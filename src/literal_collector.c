/*
 * SUPPORT FOR COMPUTING IMPLICANTS
 */

/*
 * Given a model M and a formula f such that M satisfies f, we want to
 * compute an implicant for f. The implicant is a set/conjunction of
 * literals p_1 .... p_n such that:
 *  1) every p_i is true in M
 *  2) p_1 /\ p_2 /\ ... /\ p_n => f (is valid)
 *
 *
 * To deal with if-then-else, we generalize the problem as follows:
 * - given a model M and a term t, collect a set of literals
 *   p_1 .... p_n and a term u such that
 *   1) every p_i is true in M
 *   2) p_1 /\ p_2 /\ ... /\ p_n => (t == u)
 *   3) u is atomic:
 *      if t is Boolean, u is either true_term or false_term
 *      otherwise u a term with no if-then-else subterms
 *      (e.g., u is an arithmetic term with no if-then-else).
 *
 * - informally, u is the result of simplifying t modulo p_1 ... p_n
 * - example:
 *   processing  2 + (ite (< x y) x y) may return
 *    literal: (< x y)
 *    simplified term: 2 + x
 *    if (< x y) is true in M
 *
 * Then to get the implicant for a formula f, we process f, the simplified
 * term should be true and the set of literals collected imply f.
 *
 */

#include <stdbool.h>

#include "assert_utils.h"
#include "literal_collector.h"


/*
 * Initialization: prepare collector for model mdl
 * - collect->env is not touched.
 */
void init_lit_collector(lit_collector_t *collect, model_t *mdl) {
  collect->terms = mdl->terms;
  collect->model = mdl;
  init_evaluator(&collect->eval, mdl);
  init_term_manager(&collect->manager, mdl->terms);
  init_int_hmap(&collect->cache, 0);
  init_int_hset(&collect->lit_set, 0);
  init_istack(&collect->stack);
}


/*
 * Delete everything
 */
void delete_lit_collector(lit_collector_t *collect) {
  delete_evaluator(&collect->eval);
  delete_term_manager(&collect->manager);
  delete_int_hmap(&collect->cache);
  delete_int_hset(&collect->lit_set);
  delete_istack(&collect->stack);
}


/*
 * Reset: empty the lit_set and the cache
 */
void reset_lit_collector(lit_collector_t *collect) {
  int_hmap_reset(&collect->cache);
  int_hset_reset(&collect->lit_set);
  reset_istack(&collect->stack);
}


/*
 * Get the term mapped to t in collect->cache
 * - return NULL_TERM if t is not in the cache
 */
static term_t lit_collector_find_cached_term(lit_collector_t *collect, term_t t) {
  int_hmap_pair_t *r;
  term_t u;

  assert(good_term(collect->terms, t));

  u = NULL_TERM;
  r = int_hmap_find(&collect->cache, t);
  if (r != NULL) {
    u = r->val;
    assert(good_term(collect->terms, u));
  }

  return u;
}


/*
 * Store the mapping t --> u in the cache
 */
static void lit_collector_cache_result(lit_collector_t *collect, term_t t, term_t u) {
  int_hmap_pair_t *r;

  assert(good_term(collect->terms, t) && good_term(collect->terms, u));

  r = int_hmap_get(&collect->cache, t);
  assert(r != NULL && r->val == -1);
  r->val = u;
}


/*
 * Check whether t is true in the model
 * - t must be a Boolean term
 */
static bool term_is_true_in_model(lit_collector_t *collect, term_t t) {
  value_t v;

  assert(is_boolean_term(collect->terms, t));

  v = eval_in_model(&collect->eval, t);
  if (v < 0) {
    // error in the evaluation
    longjmp(collect->env, LIT_COLLECT_EVAL_FAILED);
    // We could return false here?
  }

  return is_true(&collect->model->vtbl, v);
}


/*
 * Variant: for debugging
 */
#ifndef NDEBUG
static bool is_true_in_model(lit_collector_t *collect, term_t t) {
  value_t v;

  assert(is_boolean_term(collect->terms, t));
  v = eval_in_model(&collect->eval, t);
  return is_true(&collect->model->vtbl, v);
}
#endif


/*
 * Add t to the set of literals
 * - t must be a true in the model
 * - do nothing it t is true_term
 */
static void lit_collector_add_literal(lit_collector_t *collect, term_t t) {
  assert(is_true_in_model(collect, t));
  if (t != true_term) {
    (void) int_hset_add(&collect->lit_set, t);
  }
}


/*
 * Found an atom t:
 * - add either t or not(t) to the set of literals
 * - return true_term or false_term (i.e., value of t in the model)
 */
static term_t register_atom(lit_collector_t *collect, term_t t) {
  term_t u;

  u = true_term;
  if (! term_is_true_in_model(collect, t)) {
    u = false_term;
    t = opposite_term(t);
  }
  lit_collector_add_literal(collect, t);

  return u;
}


/*
 * RECURSIVE PROCESSING
 */

/*
 * Test equality of two arrays of terms
 */
static bool inequal_arrays(term_t *a, term_t *b, uint32_t n) {
  uint32_t i;

  for (i=0; i<n; i++) {
    if (a[i] != b[i]) return true;
  }
  return false;
}


/*
 * Variant for pprod: check whether a[i] != p->prod[i].var for some i
 * - a must have the same size as p (i.e., p->len)
 */
static bool inequal_array_pprod(term_t *a, pprod_t *p) {
  uint32_t i, n;

  n = p->len;
  for (i=0; i<n; i++) {
    if (a[i] != p->prod[i].var) return true;
  }
  return false;
}


/*
 * Variants for polynomials
 */
static bool inequal_array_poly(term_t *a, polynomial_t *p) {
  uint32_t i, n;

  n = p->nterms;
  for (i=0; i<n; i++) {
    if (a[i] != p->mono[i].var) return true;
  }

  return false;
}

static bool inequal_array_bvpoly64(term_t *a, bvpoly64_t *p) {
  uint32_t i, n;

  n = p->nterms;
  for (i=0; i<n; i++) {
    if (a[i] != p->mono[i].var) return true;
  }

  return false;
}

static bool inequal_array_bvpoly(term_t *a, bvpoly_t *p) {
  uint32_t i, n;

  n = p->nterms;
  for (i=0; i<n; i++) {
    if (a[i] != p->mono[i].var) return true;
  }

  return false;
}



/*
 * Process a term t: collect literals of t and return an atomic term
 * equal to t modulo the literals.
 */
static term_t lit_collector_visit(lit_collector_t *collect, term_t t);


/*
 * Processing of terms:
 * - input = term t + descriptor of t
 */

// t is (u == 0)
static term_t lit_collector_visit_eq_atom(lit_collector_t *collect, term_t t, term_t u) {
  term_t v;

  v = lit_collector_visit(collect, u);
  if (v != u) {
    t = mk_arith_term_eq0(&collect->manager, v);
  }
  return register_atom(collect, t);
}

// t is (u >= 0)
static term_t lit_collector_visit_ge_atom(lit_collector_t *collect, term_t t, term_t u) {
  term_t v;

  v = lit_collector_visit(collect, u);
  if (v != u) {
    t = mk_arith_term_geq0(&collect->manager, v);
  }
  return register_atom(collect, t);
}

// (ite c t1 t2)
static term_t lit_collector_visit_ite(lit_collector_t *collect, term_t t, composite_term_t *ite) {
  term_t v, u;

  assert(ite->arity == 3);
  v = lit_collector_visit(collect, ite->arg[0]); // simplify the condition
  if (v == true_term) {
    u = ite->arg[1];  // t1
  } else {
    assert(v == false_term);
    u = ite->arg[2]; // t2
  }

  return lit_collector_visit(collect, u);
}

// (apply f t1 ... t_n)
static term_t lit_collector_visit_app(lit_collector_t *collect, term_t t, composite_term_t *app) {
  term_t *a;
  uint32_t i, n;

  n = app->arity;
  assert(n >= 2);

  a = alloc_istack_array(&collect->stack, n);
  for (i=0; i<n; i++) {
    a[i] = lit_collector_visit(collect, app->arg[i]);
  }

  if (inequal_arrays(a, app->arg, n)) {
    t = mk_application(&collect->manager, a[0], n-1, a+1);
  }

  if (is_boolean_term(collect->terms, t)) {
    t = register_atom(collect, t);
  }

  free_istack_array(&collect->stack, a);

  return t;
}

// (update f t1 ... t_n v)
static term_t lit_collector_visit_update(lit_collector_t *collect, term_t t, composite_term_t *update) {
  term_t *a;
  uint32_t i, n;

  n = update->arity;
  assert(n >= 3);

  a = alloc_istack_array(&collect->stack, n);
  for (i=0; i<n; i++) {
    a[i] = lit_collector_visit(collect, update->arg[i]);
  }

  if (inequal_arrays(a, update->arg, n)) {
    t = mk_update(&collect->manager, a[0], n-2, a+1, a[n-1]);
  }

  free_istack_array(&collect->stack, a);

  return t;
}

// (tuple t1 ... t_n)
static term_t lit_collector_visit_tuple(lit_collector_t *collect, term_t t, composite_term_t *tuple) {
  term_t *a;
  uint32_t i, n;

  n = tuple->arity;
  assert(n >= 3);

  a = alloc_istack_array(&collect->stack, n);
  for (i=0; i<n; i++) {
    a[i] = lit_collector_visit(collect, tuple->arg[i]);
  }

  if (inequal_arrays(a, tuple->arg, n)) {
    t = mk_tuple(&collect->manager, n, a);
  }

  free_istack_array(&collect->stack, a);

  return t;
}

// (eq t1 t2)
static term_t lit_collector_visit_eq(lit_collector_t *collect, term_t t, composite_term_t *eq) {
  term_t t1, t2;

  assert(eq->arity == 2);
  t1 = lit_collector_visit(collect, eq->arg[0]);
  t2 = lit_collector_visit(collect, eq->arg[1]);
  if (t1 != eq->arg[0] || t2 != eq->arg[1]) {
    t = mk_eq(&collect->manager, t1, t2);
  }

  return register_atom(collect, t);
}

// (distinct t1 ... t_n)
static term_t lit_collector_visit_distinct(lit_collector_t *collect, term_t t, composite_term_t *distinct) {
  term_t *a;
  uint32_t i, n;

  n = distinct->arity;
  assert(n >= 3);

  a = alloc_istack_array(&collect->stack, n);
  for (i=0; i<n; i++) {
    a[i] = lit_collector_visit(collect, distinct->arg[i]);
  }

  if (inequal_arrays(a, distinct->arg, n)) {
    t = mk_distinct(&collect->manager, n, a);
  }

  free_istack_array(&collect->stack, a);

  return register_atom(collect, t);
}

// t is (or t1 ... t_n)
static term_t lit_collector_visit_or(lit_collector_t *collect, term_t t, composite_term_t *or) {
  term_t u;
  uint32_t i, n;

  n = or->arity;
  assert(n > 0);
  u = false_term; // prevent compilation warning

  if (term_is_true_in_model(collect, t)) {
    for (i=0; i<n; i++) {
      if (term_is_true_in_model(collect, or->arg[i])) break;
    }
    assert(i < n);
    u = lit_collector_visit(collect, or->arg[i]);
    assert(u == true_term);

  } else {
    // (or t1 ... t_n) is false --> visit all subterms
    // they should all reduce to false_term
    for (i=0; i<n; i++) {
      u = lit_collector_visit(collect, or->arg[i]);
      assert(u == false_term);
    }
  }

  return u;
}

// (xor t1 ... t_n)
static term_t lit_collector_visit_xor(lit_collector_t *collect, term_t t, composite_term_t *xor) {
  uint32_t i, n;
  term_t u;
  bool b;

  b = false;
  n = xor->arity;
  for (i=0; i<n; i++) {
    u = lit_collector_visit(collect, xor->arg[i]);
    assert(u == false_term || u == true_term);
    b ^= (u == true_term);
  }
  return bool2term(b);
}


// (arith-eq t1 t2)
static term_t lit_collector_visit_arith_bineq(lit_collector_t *collect, term_t t, composite_term_t *eq) {
  term_t t1, t2;

  assert(eq->arity == 2);
  t1 = lit_collector_visit(collect, eq->arg[0]);
  t2 = lit_collector_visit(collect, eq->arg[1]);
  if (t1 != eq->arg[0] || t2 != eq->arg[1]) {
    t = mk_arith_eq(&collect->manager, t1, t2);
  }

  return register_atom(collect, t);
}

// (bv-array t1 ... tn)
static term_t lit_collector_visit_bvarray(lit_collector_t *collect, term_t t, composite_term_t *bv) {
  term_t *a;
  uint32_t i, n;

  n = bv->arity;
  assert(n >= 1);

  a = alloc_istack_array(&collect->stack, n);
  for (i=0; i<n; i++) {
    a[i] = lit_collector_visit(collect, bv->arg[i]);
  }

  t = mk_bvarray(&collect->manager, n, a);

  free_istack_array(&collect->stack, a);

  return t;
}

// (bvdiv t1 t2)
static term_t lit_collector_visit_bvdiv(lit_collector_t *collect, term_t t, composite_term_t *bvdiv) {
  term_t t1, t2;

  assert(bvdiv->arity == 2);
  t1 = lit_collector_visit(collect, bvdiv->arg[0]);
  t2 = lit_collector_visit(collect, bvdiv->arg[1]);
  if (t1 != bvdiv->arg[0] || t2 != bvdiv->arg[1]) {
    t = mk_bvdiv(&collect->manager, t1, t2);
  }

  return t;
}

// (bvrem t1 t2)
static term_t lit_collector_visit_bvrem(lit_collector_t *collect, term_t t, composite_term_t *bvrem) {
  term_t t1, t2;

  assert(bvrem->arity == 2);
  t1 = lit_collector_visit(collect, bvrem->arg[0]);
  t2 = lit_collector_visit(collect, bvrem->arg[1]);
  if (t1 != bvrem->arg[0] || t2 != bvrem->arg[1]) {
    t = mk_bvrem(&collect->manager, t1, t2);
  }

  return t;
}

// (bvsdiv t1 t2)
static term_t lit_collector_visit_bvsdiv(lit_collector_t *collect, term_t t, composite_term_t *bvsdiv) {
  term_t t1, t2;

  assert(bvsdiv->arity == 2);
  t1 = lit_collector_visit(collect, bvsdiv->arg[0]);
  t2 = lit_collector_visit(collect, bvsdiv->arg[1]);
  if (t1 != bvsdiv->arg[0] || t2 != bvsdiv->arg[1]) {
    t = mk_bvsdiv(&collect->manager, t1, t2);
  }

  return t;
}

// (bvsrem t1 t2)
static term_t lit_collector_visit_bvsrem(lit_collector_t *collect, term_t t, composite_term_t *bvsrem) {
  term_t t1, t2;

  assert(bvsrem->arity == 2);
  t1 = lit_collector_visit(collect, bvsrem->arg[0]);
  t2 = lit_collector_visit(collect, bvsrem->arg[1]);
  if (t1 != bvsrem->arg[0] || t2 != bvsrem->arg[1]) {
    t = mk_bvsrem(&collect->manager, t1, t2);
  }

  return t;
}

// (bvsmod t1 t2)
static term_t lit_collector_visit_bvsmod(lit_collector_t *collect, term_t t, composite_term_t *bvsmod) {
  term_t t1, t2;

  assert(bvsmod->arity == 2);
  t1 = lit_collector_visit(collect, bvsmod->arg[0]);
  t2 = lit_collector_visit(collect, bvsmod->arg[1]);
  if (t1 != bvsmod->arg[0] || t2 != bvsmod->arg[1]) {
    t = mk_bvsmod(&collect->manager, t1, t2);
  }

  return t;
}

// (bvshl t1 t2)
static term_t lit_collector_visit_bvshl(lit_collector_t *collect, term_t t, composite_term_t *bvshl) {
  term_t t1, t2;

  assert(bvshl->arity == 2);
  t1 = lit_collector_visit(collect, bvshl->arg[0]);
  t2 = lit_collector_visit(collect, bvshl->arg[1]);
  if (t1 != bvshl->arg[0] || t2 != bvshl->arg[1]) {
    t = mk_bvshl(&collect->manager, t1, t2);
  }

  return t;
}

// (bvlshr t1 t2)
static term_t lit_collector_visit_bvlshr(lit_collector_t *collect, term_t t, composite_term_t *bvlshr) {
  term_t t1, t2;

  assert(bvlshr->arity == 2);
  t1 = lit_collector_visit(collect, bvlshr->arg[0]);
  t2 = lit_collector_visit(collect, bvlshr->arg[1]);
  if (t1 != bvlshr->arg[0] || t2 != bvlshr->arg[1]) {
    t = mk_bvlshr(&collect->manager, t1, t2);
  }

  return t;
}

// (bvashr t1 t2)
static term_t lit_collector_visit_bvashr(lit_collector_t *collect, term_t t, composite_term_t *bvashr) {
  term_t t1, t2;

  assert(bvashr->arity == 2);
  t1 = lit_collector_visit(collect, bvashr->arg[0]);
  t2 = lit_collector_visit(collect, bvashr->arg[1]);
  if (t1 != bvashr->arg[0] || t2 != bvashr->arg[1]) {
    t = mk_bvashr(&collect->manager, t1, t2);
  }

  return t;
}

// (bveq t1 t2)
static term_t lit_collector_visit_bveq(lit_collector_t *collect, term_t t, composite_term_t *bveq) {
  term_t t1, t2;

  assert(bveq->arity == 2);
  t1 = lit_collector_visit(collect, bveq->arg[0]);
  t2 = lit_collector_visit(collect, bveq->arg[1]);
  if (t1 != bveq->arg[0] || t2 != bveq->arg[1]) {
    t = mk_bveq(&collect->manager, t1, t2);
  }

  return register_atom(collect, t);
}

// (bvge t1 t2)
static term_t lit_collector_visit_bvge(lit_collector_t *collect, term_t t, composite_term_t *bvge) {
  term_t t1, t2;

  assert(bvge->arity == 2);
  t1 = lit_collector_visit(collect, bvge->arg[0]);
  t2 = lit_collector_visit(collect, bvge->arg[1]);
  if (t1 != bvge->arg[0] || t2 != bvge->arg[1]) {
    t = mk_bvge(&collect->manager, t1, t2);
  }

  return register_atom(collect, t);
}

// (bvsge t1 t2)
static term_t lit_collector_visit_bvsge(lit_collector_t *collect, term_t t, composite_term_t *bvsge) {
  term_t t1, t2;

  assert(bvsge->arity == 2);
  t1 = lit_collector_visit(collect, bvsge->arg[0]);
  t2 = lit_collector_visit(collect, bvsge->arg[1]);
  if (t1 != bvsge->arg[0] || t2 != bvsge->arg[1]) {
    t = mk_bvsge(&collect->manager, t1, t2);
  }

  return register_atom(collect, t);
}

// (select i u)
static term_t lit_collector_visit_select(lit_collector_t *collect, term_t t, select_term_t *select) {
  term_t u, v;
  uint32_t i;

  /*
   * select may become an invalid pointer if new terms are created
   * so we extract u and i before recursive calls to visit.
   */
  u = select->arg;
  i = select->idx;

  v = lit_collector_visit(collect, u);
  if (v != u) {
    t = mk_select(&collect->manager, i, v);
  }

  if (is_boolean_term(collect->terms, t)) {
    t = register_atom(collect, t);
  }

  return t;
}

// (bit i u)
static term_t lit_collector_visit_bit(lit_collector_t *collect, term_t t, select_term_t *bit) {
  term_t u, v;
  uint32_t i;

  /*
   * bit may become an invalid pointer if new terms are created
   * so we extract u and i before recursive calls to visit.
   */
  u = bit->arg;
  i = bit->idx;

  v = lit_collector_visit(collect, u);
  if (v != u) {
    t = mk_bitextract(&collect->manager, v, i);
  }

  return register_atom(collect, t);
}

// power product
static term_t lit_collector_visit_pprod(lit_collector_t *collect, term_t t, pprod_t *p) {
  term_t *a;
  uint32_t i, n;

  n = p->len;
  a = alloc_istack_array(&collect->stack, n);
  for (i=0; i<n; i++) {
    a[i] = lit_collector_visit(collect, p->prod[i].var);
  }

  if (inequal_array_pprod(a, p)) {
    t = mk_pprod(&collect->manager, p, n, a);
  }

  free_istack_array(&collect->stack, a);

  return t;
}

// polynomial (rational coefficients)
static term_t lit_collector_visit_poly(lit_collector_t *collect, term_t t, polynomial_t *p) {
  term_t *a;
  uint32_t i, n;

  n = p->nterms;
  a = alloc_istack_array(&collect->stack, n);

  // skip the constant term if any
  i = 0;
  if (p->mono[0].var == const_idx) {
    a[i] = const_idx;
    i ++;
  }

  // rest of p
  while (i < n) {
    a[i] = lit_collector_visit(collect, p->mono[i].var);
    i ++;
  }

  if (inequal_array_poly(a, p)) {
    t = mk_arith_poly(&collect->manager, p, n, a);
  }

  free_istack_array(&collect->stack, a);

  return t;
}

// bitvector polynomial (coefficients are 64bit or less)
static term_t lit_collector_visit_bvpoly64(lit_collector_t *collect, term_t t, bvpoly64_t *p) {
  term_t *a;
  uint32_t i, n;

  n = p->nterms;
  a = alloc_istack_array(&collect->stack, n);

  // skip the constant term if any
  i = 0;
  if (p->mono[0].var == const_idx) {
    a[i] = const_idx;
    i ++;
  }

  // rest of p
  while (i < n) {
    a[i] = lit_collector_visit(collect, p->mono[i].var);
    i ++;
  }

  if (inequal_array_bvpoly64(a, p)) {
    t = mk_bvarith64_poly(&collect->manager, p, n, a);
  }

  free_istack_array(&collect->stack, a);

  return t;
}

// bitvector polynomials (coefficients more than 64bits)
static term_t lit_collector_visit_bvpoly(lit_collector_t *collect, term_t t, bvpoly_t *p) {
  term_t *a;
  uint32_t i, n;

  n = p->nterms;
  a = alloc_istack_array(&collect->stack, n);

  // skip the constant term if any
  i = 0;
  if (p->mono[0].var == const_idx) {
    a[i] = const_idx;
    i ++;
  }

  // rest of p
  while (i < n) {
    a[i] = lit_collector_visit(collect, p->mono[i].var);
    i ++;
  }

  if (inequal_array_bvpoly(a, p)) {
    t = mk_bvarith_poly(&collect->manager, p, n, a);
  }

  free_istack_array(&collect->stack, a);

  return t;
}


/*
 * Process term t:
 * - if t is in the cache (already visited) return the corresponding term
 * - otherwise explore t and return its simplified version
 * - also add atoms found while exploring t
 */
static term_t lit_collector_visit(lit_collector_t *collect, term_t t) {
  term_table_t *terms;
  uint32_t polarity;
  term_t u;

  polarity = polarity_of(t);
  t = unsigned_term(t);

  u = lit_collector_find_cached_term(collect, t);
  if (u == NULL_TERM) {
    terms = collect->terms;
    switch (term_kind(terms, t)) {
    case CONSTANT_TERM:
    case ARITH_CONSTANT:
    case BV64_CONSTANT:
    case BV_CONSTANT:
      u = t;
      break;

    case VARIABLE:
      longjmp(collect->env, LIT_COLLECT_FREEVAR_IN_TERM);
      break;

    case UNINTERPRETED_TERM:
      if (is_boolean_term(terms, t)) {
	u = register_atom(collect, t);
      } else {
	u = t;
      }
      break;

    case ARITH_EQ_ATOM:
      u = lit_collector_visit_eq_atom(collect, t, arith_eq_arg(terms, t));
      break;

    case ARITH_GE_ATOM:
      u = lit_collector_visit_ge_atom(collect, t, arith_ge_arg(terms, t));
      break;

    case ITE_TERM:
    case ITE_SPECIAL:
      u = lit_collector_visit_ite(collect, t, ite_term_desc(terms, t));
      break;

    case APP_TERM:
      u = lit_collector_visit_app(collect, t, app_term_desc(terms, t));
      break;

    case UPDATE_TERM:
      u = lit_collector_visit_update(collect, t, update_term_desc(terms, t));
      break;

    case TUPLE_TERM:
      u = lit_collector_visit_tuple(collect, t, tuple_term_desc(terms, t));
      break;

    case EQ_TERM:
      u = lit_collector_visit_eq(collect, t, eq_term_desc(terms, t));
      break;

    case DISTINCT_TERM:
      u = lit_collector_visit_distinct(collect, t, distinct_term_desc(terms, t));
      break;

    case FORALL_TERM:
      longjmp(collect->env, LIT_COLLECT_QUANTIFIER);
      break;

    case LAMBDA_TERM:
      longjmp(collect->env, LIT_COLLECT_LAMBDA);
      break;

    case OR_TERM:
      u = lit_collector_visit_or(collect, t, or_term_desc(terms, t));
      break;

    case XOR_TERM:
      u = lit_collector_visit_xor(collect, t, xor_term_desc(terms, t));
      break;

    case ARITH_BINEQ_ATOM:
      u = lit_collector_visit_arith_bineq(collect, t, arith_bineq_atom_desc(terms, t));
      break;

    case BV_ARRAY:
      u = lit_collector_visit_bvarray(collect, t, bvarray_term_desc(terms, t));
      break;

    case BV_DIV:
      u = lit_collector_visit_bvdiv(collect, t, bvdiv_term_desc(terms, t));
      break;

    case BV_REM:
      u = lit_collector_visit_bvrem(collect, t, bvrem_term_desc(terms, t));
      break;

    case BV_SDIV:
      u = lit_collector_visit_bvsdiv(collect, t, bvsdiv_term_desc(terms, t));
      break;

    case BV_SREM:
      u = lit_collector_visit_bvsrem(collect, t, bvsrem_term_desc(terms, t));
      break;

    case BV_SMOD:
      u = lit_collector_visit_bvsmod(collect, t, bvsmod_term_desc(terms, t));
      break;

    case BV_SHL:
      u = lit_collector_visit_bvshl(collect, t, bvshl_term_desc(terms, t));
      break;

    case BV_LSHR:
      u = lit_collector_visit_bvlshr(collect, t, bvlshr_term_desc(terms, t));
      break;

    case BV_ASHR:
      u = lit_collector_visit_bvashr(collect, t, bvashr_term_desc(terms, t));
      break;

    case BV_EQ_ATOM:
      u = lit_collector_visit_bveq(collect, t, bveq_atom_desc(terms, t));
      break;

    case BV_GE_ATOM:
      u = lit_collector_visit_bvge(collect, t, bvge_atom_desc(terms, t));
      break;

    case BV_SGE_ATOM:
      u = lit_collector_visit_bvsge(collect, t, bvsge_atom_desc(terms, t));
      break;

    case SELECT_TERM:
      u = lit_collector_visit_select(collect, t, select_term_desc(terms, t));
      break;

    case BIT_TERM:
      u = lit_collector_visit_bit(collect, t, bit_term_desc(terms, t));
      break;

    case POWER_PRODUCT:
      u = lit_collector_visit_pprod(collect, t, pprod_term_desc(terms, t));
      break;

    case ARITH_POLY:
      u = lit_collector_visit_poly(collect, t, poly_term_desc(terms, t));
      break;

    case BV64_POLY:
      u = lit_collector_visit_bvpoly64(collect, t, bvpoly64_term_desc(terms, t));
      break;

    case BV_POLY:
      u = lit_collector_visit_bvpoly(collect, t, bvpoly_term_desc(terms, t));
      break;

    case UNUSED_TERM:
    case RESERVED_TERM:
    default:
      assert(false);
      longjmp(collect->env, LIT_COLLECT_INTERNAL_ERROR);
      break;
    }
    lit_collector_cache_result(collect, t, u);
  }

  return u ^ polarity;
}


/*
 * Top-level call: process term t:
 * - return an atomic term u equal to t  modulo the literals in collect->lit_set
 * - add literals of t to collect->lit_set
 *
 * - return a negative error code if something goes wrong.
 */
term_t lit_collector_process(lit_collector_t *collect, term_t t) {
  term_t u;

  u = setjmp(collect->env);
  if (u == 0) {
    u = lit_collector_visit(collect, t);
  } else {
    assert(u < 0); // error code after longjmp
    reset_istack(&collect->stack);
  }

  return u;
}



/*
 * GET IMPLICANTS FOR ASSERTIONS GIVEN A MODEL
 */

/*
 * Given a model mdl and a set of formulas a[0 ... n-1] satisfied by mdl,
 * compute a set of implicants for a[0] /\ a[1] /\ ... /\ a[n-2].
 * - all terms in a must be Boolean and all of them must be true in mdl
 * - if there's a error, the function returns a negative code
 *   and leaves v unchanged
 * - otherwise, the function retuns 0 and add the implicants to vector
 *   v  (v is not reset).
 */
int32_t get_implicants(model_t *mdl, uint32_t n, term_t *a, ivector_t *v) {
  lit_collector_t collect;
  int_hset_t *set;
  int32_t u;
  uint32_t i;

  init_lit_collector(&collect, mdl);
  for (i=0; i<n; i++) {
    u = lit_collector_process(&collect, a[i]);
    if (u < 0) goto done;
    assert(u == true_term); // since a[i] must be true in mdl
  }

  // Extract the implicants. They are stored in collect.lit_set
  set = &collect.lit_set;
  int_hset_close(set);
  n = set->nelems;
  for (i=0; i<n; i++) {
    u = set->data[i];
    assert(is_true_in_model(&collect, u));
    ivector_push(v, u);
  }

  // return code = 0 (no error);
  u = 0;

 done:
  delete_lit_collector(&collect);
  return u;
}

