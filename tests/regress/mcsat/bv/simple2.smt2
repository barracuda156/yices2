(set-logic QF_BV)
(declare-fun _substvar_581_ () (_ BitVec 8))
(declare-fun _substvar_926_ () Bool)
(declare-fun _substvar_582_ () (_ BitVec 8))
(assert (let ((?v_14 (bvlshr (concat (_ bv1 1) (_ bv0 7)) ((_ zero_extend 5) (_ bv1 3))))) (let ((?v_20 (ite (= (_ bv1 1) (bvnot (ite (bvult ?v_14 _substvar_581_) (_ bv1 1) (_ bv0 1)))) ?v_14 _substvar_582_)) (?v_17 (bvlshr (concat (_ bv1 1) (_ bv0 7)) ((_ zero_extend 5) (_ bv1 3))))) (not (= (bvand (bvnot (ite (bvult _substvar_582_ _substvar_581_) (_ bv1 1) (_ bv0 1))) (ite (= (ite _substvar_926_ (bvand ?v_20 (bvnot (bvlshr (concat (_ bv1 1) (_ bv0 7)) ((_ zero_extend 5) (_ bv1 3))))) ?v_20) (_ bv0 8)) (_ bv1 1) (_ bv0 1))) (_ bv0 1))))))
(check-sat)
(exit)