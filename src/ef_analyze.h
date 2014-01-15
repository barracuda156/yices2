/*
 * Processing of terms t as part EF-solving
 */

/*
 * All processing is based on the convention that uninterpreted terms
 * represent existential variables and any variable is universal.
 *
 * Example assertion:
 *
 *   (and (<= 0 x) (<= x 10)  (forall y: (=> (<= y 10) (< (* y x) 5)))
 *
 * In the internal representation: 
 * - x is an uninterpreted term
 * - y is a variable
 * These are syntactically different objects
 *
 * After flattening and stripping away the universal quantifiers, we 
 * get three formulas:
 *   (<= 0 x)
 *   (<= x 10)
 *   (=> (<= y 10) (< (* y x) 5))
 * 
 * We can still extract universal and existential variables from these:
 * - any uninterpreted term is considered an existential variable (e.g., x)
 * - any (free) variable is considered a universal variable (e.g., y).
 */

#ifndef __EF_ANALYZE_H
#define __EF_ANALYZE_H

#include <stdint.h>
#include <stdbool.h>

#include "int_queues.h"
#include "int_vectors.h"
#include "int_hash_sets.h"
#include "term_manager.h"



/*
 * EF clause = a disjunction of formulas: assumptions and guarantees
 * - formulas that contain only universal variables (no existential variables)
 *   are stored in the assumptions vector
 * - other formulas are stored in the guarantees vector
 * - the existential variables are stored in evars
 * - the universal variables are stored in uvars
 */
typedef struct ef_clause_s {
  ivector_t evars; // existential variables
  ivector_t uvars; // universal variables
  ivector_t assumptions;
  ivector_t guarantees;
} ef_clause_t;


/*
 * EF analyzer: to process/decompose an EF-problem
 * - terms = term table where all terms are defined
 * - manager = relevant term mamager
 * - queue = queue to explore terms/subterms
 * - cache = set of already visited terms
 * - flat = vector of assertions (result of flattening)
 * - disjuncts = vector of formula (or-flattening of assertions)
 * - evars = vector to collect existential variables (uninterpreted terms)
 * - uvars = vector to collect universal variables (variables)
 */
typedef struct ef_analyzer_s {
  term_table_t *terms;
  term_manager_t *manager;
  int_queue_t queue;
  int_hset_t cache;
  ivector_t flat;
  ivector_t disjuncts;
  ivector_t evars;
  ivector_t uvars;
} ef_analyzer_t;




/*
 * OPERATIONS ON CLAUSES
 */

/*
 * Initialize all vectors
 */
extern void init_ef_clause(ef_clause_t *cl);

/*
 * Empty all vectors
 */
extern void reset_ef_clause(ef_clause_t *cl);

/*
 * Delete the whole thing
 */
extern void delete_ef_clause(ef_clause_t *cl);



/*
 * ANALYZER
 */

/*
 * Initialize the data structure
 */
extern void init_ef_analyzer(ef_analyzer_t *ef, term_manager_t *mngr);


/*
 * Reset: empty cache and queue and internal vectors
 */
extern void reset_ef_analyzer(ef_analyzer_t *ef);


/*
 * Free all memory used
 */
extern void delete_ef_analyzer(ef_analyzer_t *ef);


/*
 * Add assertions and flatten them to conjuncts
 * - n = number of assertions
 * - a = array of n assertions
 *
 * - any formula a[i] of the form (and A B ...) is flattened
 *   also any formula a[i] of the form (forall y : C) is replaced by C
 *   this is done recursively, and the result is stored in vector v
 * 
 * - optional processing: 
 *   if f_ite is true, flatten (ite c a b) to (c => a) and (not c => b)
 *   if f_iff is true, flatten (iff a b)   to (a => b) and (b => a)
 *
 * Note: this does not do type checking. If any term in a is not Boolean,
 * it is kept as is in the ef->flat vector.
 */
extern void ef_add_assertions(ef_analyzer_t *ef, uint32_t n, term_t *a, bool f_ite, bool f_iff, ivector_t *v);


/*
 * Convert t to a set of disjuncts
 * - the result is stored in vector v
 * - optional processing:
 *   if f_ite is true (ite c a b) is rewritten to (c and a) or ((not c) and b)
 *   if f_iff is true (iff a b)   is rewritten to (a and b) or ((not a) and (not b))
 */
extern void ef_flatten_to_disjuncts(ef_analyzer_t *ef, term_t t, bool f_ite, bool f_iff, ivector_t *v);


/*
 * Collect variables of t and check that it's quantifier free
 * - return true if t is quantifier free
 * - return false otherwise
 * - collect the variables of t in vector uvar (universal vars)
 * - collect the uninterpreted constants of t in vector evar (existential vars)
 */
extern bool ef_get_vars(ef_analyzer_t *ef, term_t t, ivector_t *uvar, ivector_t *evar);



/*
 * Check that all variables in vector v have atomic type
 * - i.e., no variable of tuple type or function type
 */
extern bool all_atomic_vars(ef_analyzer_t *ef, ivector_t *v);


/*
 * Check that all uninterpreted terms in vector v have atomic type
 * or are uninterpreted function on atomic types.
 * - e.g., this returns false if v[i] has tuple type or a type like (-> (-> int bool) bool)
 */
extern bool all_basic_vars(ef_analyzer_t *ef, ivector_t *v);


/*
 * Remove all uninterpreted functions from v (i.e., all terms with function type).
 * - this is intended to be used for v that satisfies all_basic_vars
 * - return the number of terms removed
 */
extern uint32_t remove_uninterpreted_functions(ef_analyzer_t *ef, ivector_t *v);


/*
 * Decompose term t into an Exist/Forall clause
 * - t is written to (or A_1(y) .... A_k(y) G_1(x, y) ... G_t(x, y))
 *   where x = uninterpreted constants of t (existentials)
 *     and y = free variables of t (universal variables)
 * - A_i = any term that contains only the y variables
 *   G_j = any other term
 * - the set of universal variables are collected in c->uvars
 *   the set of existential variables are collected in c->evars
 *   the A_i's are stored in c->assumptions
 *   the G_j's are stored in c->guarantees
 */
extern void ef_decompose(ef_analyzer_t *ef, term_t t, ef_clause_t *c);




#endif /* __EF_ANALYZE_H */
