/** @file poisson2_example.cpp

    @brief Tutorial on how to use expression assembler to solve the Poisson equation

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): A. Mantzaflaris
*/

//! [Include namespace]
#include <typeinfo>
#include <gismo.h>

#  define MatExprType  auto

namespace gismo{
namespace expr{

// Comments for var1:
// - TODO: dimensionm indep. later on
template<class E>
class var1_expr : public _expr<var1_expr<E> >
{
public:
    typedef typename E::Scalar Scalar;

private:

    typename E::Nested_t _u;
    typename gsGeometryMap<Scalar>::Nested_t _G;

public:
    enum{ Space = E::Space };

    var1_expr(const E & u, const gsGeometryMap<Scalar> & G) : _u(u), _G(G) { }

    mutable gsMatrix<Scalar> res;

    mutable gsMatrix<Scalar> bGrads, cJac;
    mutable gsVector<Scalar,3> m_v, normal;
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    // helper function
    static inline gsVector<Scalar,3> vecFun(index_t pos, Scalar val)
    {
        gsVector<Scalar,3> result = gsVector<Scalar,3>::Zero();
        result[pos] = val;
        return result;
    }

    MatExprType eval(const index_t k) const
    {
        const index_t A = _u.cardinality()/_u.targetDim();
        res.resize(A*_u.targetDim(), cols()); // rows()*

        normal = _G.data().normal(k);// not normalized to unit length
        normal.normalize();
        bGrads = _u.data().values[1].col(k);
        cJac = _G.data().values[1].reshapeCol(k, _G.data().dim.first, _G.data().dim.second).transpose();
        const Scalar measure =  _G.data().measures.at(k);

        // gsDebugVar(_G.data().values[0].col(k).transpose());

        for (index_t d = 0; d!= cols(); ++d) // for all basis function components
        {
            const short_t s = d*A;
            for (index_t j = 0; j!= A; ++j) // for all actives
            {
                // Jac(u) ~ Jac(G) with alternating signs ?..
                m_v.noalias() = (vecFun(d, bGrads.at(2*j  ) ).cross( cJac.col(1).template head<3>() )
                              - vecFun(d, bGrads.at(2*j+1) ).cross( cJac.col(0).template head<3>() )) / measure;

                // ---------------  First variation of the normal
                // res.row(s+j).noalias() = (m_v - ( normal.dot(m_v) ) * normal).transpose();
                res.row(s+j).noalias() = (m_v - ( normal*m_v.transpose() ) * normal).transpose(); // outer-product version
            }
        }
        return res;
    }

    index_t rows() const { return 1; }
    index_t cols() const { return _u.dim(); }

    void setFlag() const
    {
        _u.data().flags |= NEED_GRAD | NEED_ACTIVE;
        _G.data().flags |= NEED_NORMAL | NEED_DERIV | NEED_MEASURE;
    }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    {
        evList.push_sorted_unique(&_u.source());
        _u.data().flags |= NEED_GRAD;
        _G.data().flags |= NEED_NORMAL | NEED_DERIV | NEED_MEASURE;
    }

    const gsFeSpace<Scalar> & rowVar() const { return _u.rowVar(); }
    const gsFeSpace<Scalar> & colVar() const {return gsNullExpr<Scalar>::get();}
    index_t cardinality_impl() const { return _u.cardinality_impl(); }

    static constexpr bool rowSpan() {return E::rowSpan(); }
    static constexpr bool colSpan() {return false;}

    void print(std::ostream &os) const { os << "var1("; _u.print(os); os <<")"; }
};

// Comments for var2:
// - TODO: dimensionm indep. later on
// - TODO: how to structure this matrix
template<class E1, class E2, class E3>
class var2_expr : public _expr<var2_expr<E1,E2,E3> >
{
public:
    typedef typename E1::Scalar Scalar;

private:

    typename E1::Nested_t _u;
    typename E2::Nested_t _v;
    typename gsGeometryMap<Scalar>::Nested_t _G;
    typename E3::Nested_t _Ef;

public:
    enum{ Space = E1::Space };

    var2_expr( const E1 & u, const E2 & v, const gsGeometryMap<Scalar> & G, _expr<E3> const& Ef) : _u(u),_v(v), _G(G), _Ef(Ef) { }

    mutable gsMatrix<Scalar> res;

    mutable gsMatrix<Scalar> uGrads, vGrads, cJac, cDer2, evEf, result;
    mutable gsVector<Scalar> m_u, m_v, normal, m_uv, m_u_der, n_der, n_der2, tmp; // memomry leaks when gsVector<T,3>, i.e. fixed dimension
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    // helper function
    static inline gsVector<Scalar,3> vecFun(index_t pos, Scalar val)
    {
        gsVector<Scalar,3> result = gsVector<Scalar,3>::Zero();
        result[pos] = val;
        return result;
    }

    const gsMatrix<Scalar> & eval(const index_t k) const
    {
        res.resize(_u.cardinality(), _u.cardinality());

        normal = _G.data().normal(k);
        normal.normalize();
        uGrads = _u.data().values[1].col(k);
        vGrads = _v.data().values[1].col(k);
        cJac = _G.data().values[1].reshapeCol(k, _G.data().dim.first, _G.data().dim.second).transpose();
        cDer2 = _G.data().values[2].reshapeCol(k, _G.data().dim.second, _G.data().dim.second);

        const index_t cardU = _u.data().values[0].rows(); // number of actives per component of u
        const index_t cardV = _v.data().values[0].rows(); // number of actives per component of v
        const Scalar measure =  _G.data().measures.at(k);

        evEf = _Ef.eval(k);

        for (index_t j = 0; j!= cardU; ++j) // for all basis functions u (1)
        {
            for (index_t i = 0; i!= cardV; ++i) // for all basis functions v (1)
            {
                for (index_t d = 0; d!= _u.dim(); ++d) // for all basis functions u (2)
                {
                    m_u.noalias() = ( vecFun(d, uGrads.at(2*j  ) ).cross( cJac.col(1).template head<3>() )
                                     -vecFun(d, uGrads.at(2*j+1) ).cross( cJac.col(0).template head<3>() ))
                                    / measure;

                    const short_t s = d*cardU;

                    for (index_t c = 0; c!= _v.dim(); ++c) // for all basis functions v (2)
                    {
                        const short_t r = c*cardV;
                        m_v.noalias() = ( vecFun(c, vGrads.at(2*i  ) ).cross( cJac.col(1).template head<3>() )
                                         -vecFun(c, vGrads.at(2*i+1) ).cross( cJac.col(0).template head<3>() ))
                                        / measure;

                        // n_der.noalias() = (m_v - ( normal.dot(m_v) ) * normal);
                        n_der.noalias() = (m_v - ( normal*m_v.transpose() ) * normal); // outer-product version

                        m_uv.noalias() = ( vecFun(d, uGrads.at(2*j  ) ).cross( vecFun(c, vGrads.at(2*i+1) ) )
                                          +vecFun(c, vGrads.at(2*i  ) ).cross( vecFun(d, uGrads.at(2*j+1) ) ))
                                          / measure; //check

                        m_u_der.noalias() = (m_uv - ( normal.dot(m_v) ) * m_u);
                        // m_u_der.noalias() = (m_uv - ( normal*m_v.transpose() ) * m_u); // outer-product version TODO

                        // ---------------  Second variation of the normal
                        tmp = m_u_der - (m_u.dot(n_der) + normal.dot(m_u_der) ) * normal - (normal.dot(m_u) ) * n_der;
                        // tmp = m_u_der - (m_u.dot(n_der) + normal.dot(m_u_der) ) * normal - (normal.dot(m_u) ) * n_der;

                        // Evaluate the product
                        tmp = cDer2 * tmp; // E_f_der2, last component
                        tmp.row(2) *= 2.0;
                        result = evEf * tmp;

                        res(s + j, r + i ) = result(0,0);
                    }
                }
            }
        }
        return res;
    }

    index_t rows() const
    {
        return 1; // because the resulting matrix has scalar entries for every combination of active basis functions
    }

    index_t cols() const
    {
        return 1; // because the resulting matrix has scalar entries for every combination of active basis functions
    }

    void setFlag() const
    {
        _u.data().flags |= NEED_GRAD;
        _G.data().flags |= NEED_NORMAL | NEED_DERIV | NEED_2ND_DER | NEED_MEASURE;
        _Ef.setFlag();
    }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    {
        evList.push_sorted_unique(&_u.source());
        _u.data().flags |= NEED_GRAD;
        _G.data().flags |=  NEED_NORMAL | NEED_DERIV | NEED_2ND_DER | NEED_MEASURE;
        _Ef.setFlag();
    }

    const gsFeSpace<Scalar> & rowVar() const { return _u.rowVar(); }
    const gsFeSpace<Scalar> & colVar() const { return _v.rowVar(); }

    static constexpr bool rowSpan() {return true; }
    static constexpr bool colSpan() {return true; }

    void print(std::ostream &os) const { os << "var2("; _u.print(os); os <<")"; }
};


template<class E1, class E2>
class deriv2dot_expr : public _expr<deriv2dot_expr<E1, E2> >
{
public:
    typedef typename E1::Scalar Scalar;

private:

    typename E1::Nested_t _u;
    typename E2::Nested_t _v;

public:
    enum{ Space = E1::Space };

    deriv2dot_expr(const E1 & u, const E2 & v) : _u(u), _v(v) { }

    mutable gsMatrix<Scalar> res,tmp, vEv;

    const gsMatrix<Scalar> & eval(const index_t k) const {return eval_impl(_u,k); }

    index_t rows() const
    {
        return 1; //since the product with another vector is computed
    }

    index_t cols() const
    {
        return _u.source().domainDim() * ( _u.source().domainDim() + 1 ) / 2;
    }

    void setFlag() const
    {
        _u.data().flags |= NEED_DERIV2;
        _v.setFlag();
    }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    {
        evList.push_sorted_unique(&_u.source());
        _u.data().flags |= NEED_DERIV2;
    }

    const gsFeSpace<Scalar> & rowVar() const { return _u.rowVar(); }
    const gsFeSpace<Scalar> & colVar() const { return _v.rowVar(); }

    static constexpr bool rowSpan() {return E1::rowSpan(); }
    static constexpr bool colSpan() {return E2::rowSpan(); }

    void print(std::ostream &os) const { os << "deriv2("; _u.print(os); _v.print(os); os <<")"; }

private:
    template<class U> inline
    typename util::enable_if< util::is_same<U,gsGeometryMap<Scalar> >::value, const gsMatrix<Scalar> & >::type
    eval_impl(const U & u, const index_t k)  const
    {
        /*
            Here, we multiply the hessian of the geometry map by a vector, which possibly has multiple actives.
            The hessian of the geometry map c has the form: hess(c)
            [d11 c1, d11 c2, d11 c3]
            [d22 c1, d22 c2, d22 c3]
            [d12 c1, d12 c2, d12 c3]
            And we want to compute [d11 c .v; d22 c .v;  d12 c .v] ( . denotes a dot product and c and v are both vectors)
            So we simply evaluate for every active basis function v_k the product hess(c).v_k
        */


        // evaluate the geometry map of U
        tmp =_u.data().values[2].reshapeCol(k, cols(), _u.data().dim.second );
        vEv = _v.eval(k);
        res = vEv * tmp.transpose();
        return res;
    }

    template<class U> inline
    typename util::enable_if<util::is_same<U,gsFeSpace<Scalar> >::value, const gsMatrix<Scalar> & >::type
    eval_impl(const U & u, const index_t k) const
    {
        /*
            We assume that the basis has the form v*e_i where e_i is the unit vector with 1 on index i and 0 elsewhere
            This implies that hess(v) = [hess(v_1), hess(v_2), hess(v_3)] only has nonzero entries in column i. Hence,
            hess(v) . normal = hess(v_i) * n_i (vector-scalar multiplication. The result is then of the form
            [hess(v_1)*n_1 .., hess(v_1)*n_1 .., hess(v_1)*n_1 ..]. Here, the dots .. represent the active basis functions.
        */
        const index_t numAct = u.data().values[0].rows();   // number of actives of a basis function
        const index_t cardinality = u.cardinality();        // total number of actives (=3*numAct)
        res.resize(rows()*cardinality, cols() );
        tmp.transpose() =_u.data().values[2].reshapeCol(k, cols(), numAct );
        vEv = _v.eval(k);

        for (index_t i = 0; i!=_u.dim(); i++)
            res.block(i*numAct, 0, numAct, cols() ) = tmp * vEv.at(i);
        return res;
    }

};


/*
    The deriv2_expr computes the hessian of a basis.
    It assumes that the vector of basis functions is of the form v = u*e_i where u
    is the scalar basis function u: [0,1]^3 -> R^1 and e_i is the unit vector with a 1 on index i and a 0 elsewhere.
    Let us define the following blocks
    hess1(u) =              hess2(u) =              hess3(u) =
    [d11 u , 0 , 0 ]    |   [0 , d11 u , 0 ]     |  [0 , 0 , d11 u ]
    [d22 u , 0 , 0 ]    |   [0 , d22 u , 0 ]     |  [0 , 0 , d22 u ]
    [d12 u , 0 , 0 ]    |   [0 , d12 u , 0 ]     |  [0 , 0 , d12 u ]

    Then the deriv2(u) is defined as follows (for k number of actives)
    [hess1(u)_1]
    ...
    [hess1(u)_k]
    [hess2(u)_1]
    ...
    [hess2(u)_k]
    [hess3(u)_1]
    ...
    [hess3(u)_k]
**/
template<class E>
class deriv2_expr : public _expr<deriv2_expr<E> >
{
public:
    typedef typename E::Scalar Scalar;

private:

    typename E::Nested_t _u;

public:
    enum {ColBlocks = E::rowSpan() };
    enum{ Space = E::Space };

    deriv2_expr(const E & u) : _u(u) { }

    mutable gsMatrix<Scalar> res, tmp;

    const gsMatrix<Scalar> & eval(const index_t k) const {return eval_impl(_u,k); }

    index_t rows() const //(components)
    {
        // return _u.data().values[2].rows() / _u.data().values[0].rows(); // numHessian dimensions
        // return _u.source().targetDim(); // no. dimensions should be 3
        return 3; // _u.dim() for space or targetDim() for geometry
    }

    index_t cols() const
    {
        return _u.source().domainDim() * ( _u.source().domainDim() + 1 ) / 2;
    }

    void setFlag() const
    {
        _u.data().flags |= NEED_DERIV2|NEED_ACTIVE;
    }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    {
        evList.push_sorted_unique(&_u.source());
        _u.data().flags |= NEED_DERIV2;
    }

    const gsFeSpace<Scalar> & rowVar() const { return _u.rowVar(); }
    const gsFeSpace<Scalar> & colVar() const { return _u.colVar(); }

    index_t cardinality_impl() const { return _u.cardinality_impl(); }

    static constexpr bool rowSpan() {return E::rowSpan();}
    static constexpr bool colSpan() {return E::colSpan();}

    void print(std::ostream &os) const { os << "deriv2("; _u.print(os); os <<")"; }

    private:
        template<class U> inline
        typename util::enable_if< util::is_same<U,gsGeometryMap<Scalar> >::value, const gsMatrix<Scalar> & >::type
        eval_impl(const U & u, const index_t k)  const
        {
            /*
                Here, we compute the hessian of the geometry map.
                The hessian of the geometry map c has the form: hess(c)
                [d11 c1, d11 c2, d11 c3]
                [d22 c1, d22 c2, d22 c3]
                [d12 c1, d12 c2, d12 c3]

                The geometry map has components c=[c1,c2,c3]
            */
            // evaluate the geometry map of U
            res = _u.data().values[2].reshapeCol(k, cols(), _u.data().dim.second );
            return res;
        }

        template<class U> inline
        typename util::enable_if<util::is_same<U,gsFeSpace<Scalar> >::value, const gsMatrix<Scalar> & >::type
        eval_impl(const U & u, const index_t k) const
        {
            /*
                Here, we compute the hessian of the basis with n actives.
                The hessian of the basis u has the form: hess(u)
                    active 1                active 2                        active n = cardinality
                [d11 u1, d11 u2, d11 u3] [d11 u1, d11 u2, d11 u3] ... [d11 u1, d11 u2, d11 u3]
                [d22 u1, d22 u2, d22 u3] [d22 u1, d22 u2, d22 u3] ... [d22 u1, d22 u2, d22 u3]
                [d12 u1, d12 u2, d12 u3] [d12 u1, d12 u2, d12 u3] ... [d12 u1, d12 u2, d12 u3]

                Here, the basis function has components u = [u1,u2,u3]. Since they are evaluated for scalars
                we use blockDiag to make copies for all components ui

                    active 1     active 2     active k = cardinality/dim   active 1           active 2k       active 1           active 2k
                [d11 u, 0, 0] [d11 u, 0, 0] ... [d11 u, 0, 0]            [0, d11 u, 0]  ... [0, d11 u, 0]  [0, d11 u, 0]  ... [0, d11 u, 0]
                [d22 u, 0, 0] [d22 u, 0, 0] ... [d22 u, 0, 0]            [0, d22 u, 0]  ... [0, d22 u, 0]  [0, d22 u, 0]  ... [0, d22 u, 0]
                [d12 u, 0, 0] [d12 u, 0, 0] ... [d12 u, 0, 0]            [0, d12 u, 0]  ... [0, d12 u, 0]  [0, d12 u, 0]  ... [0, d12 u, 0]

            */
            const index_t numAct = u.data().values[0].rows();   // number of actives of a basis function
            const index_t cardinality = u.cardinality();        // total number of actives (=3*numAct)

            res.resize(rows(), _u.dim() *_u.cardinality()); // (3 x 3*cardinality)
            res.setZero();

            tmp = _u.data().values[2].reshapeCol(k, cols(), numAct );
            for (index_t d = 0; d != cols(); ++d)
            {
                const index_t s = d*(cardinality + 1);
                for (index_t i = 0; i != numAct; ++i)
                    res.col(s+i*_u.cols()) = tmp.col(i);
            }

            // res = _u.data().values[2].col(k).transpose().blockDiag(_u.targetDim()); // analoguous to jacobian..
            // res = res.transpose();
            // gsDebugVar(res);
            return res;
        }

};



// template<class E1, class E2>
// class hessdot_expr : public _expr<hessdot_expr<E1,E2> >
// {
//     typename E1::Nested_t _u;
//     typename E2::Nested_t _v;

// public:
//     enum{ Space = E1::Space };

//     typedef typename E1::Scalar Scalar;

//     hessdot_expr(const E1 & u, const E2 & v) : _u(u), _v(v) {}

//     mutable gsMatrix<Scalar> res, hess, tmp;
//     mutable gsMatrix<Scalar> normalMat;

//     MatExprType eval(const index_t k) const
//     {
//         const gsFuncData<Scalar> & udata = _u.data(); // udata.values[2].col(k)
//         const index_t numAct = udata.values[0].rows();
//         const gsAsConstMatrix<Scalar> ders(udata.values[2].col(k).data(), 3, numAct );

//         tmp = _v.eval(k);

//         res.resize(rows(), cols() );


//             for (index_t i = 0; i!=tmp.rows(); ++i)
//             {
//                 res.block(i*numAct, 0, numAct, 3).noalias() = ders.transpose() * tmp.at(i);
//             }

//         return res;
//     }

//     index_t rows() const
//     {
//         return _u.dim() * _u.data().values[0].rows();
//     }

//     index_t cols() const
//     {
//         return // ( E2::rowSpan() ? rows() : 1 ) *
//             _u.data().values[2].rows() / _u.data().values[0].rows();//=3
//     }

//     void setFlag() const
//     {
//         _u.data().flags |= NEED_2ND_DER;
//         _v.setFlag();
//     }

//     void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
//     {
//         //GISMO_ASSERT(NULL!=m_fd, "FeVariable: FuncData member not registered");
//         evList.push_sorted_unique(&_u.source());
//         _u.data().flags |= NEED_2ND_DER;
//     }

//     const gsFeSpace<Scalar> & rowVar() const { return _u.rowVar(); }
//     const gsFeSpace<Scalar> & colVar() const { return _v.rowVar(); }

//     static constexpr bool rowSpan() {return E1::rowSpan(); }
//     static constexpr bool colSpan() {return E2::rowSpan(); }

//     void print(std::ostream &os) const { os << "hessdot("; _u.print(os); os <<")"; }
// };

/*
    Evaluates:
    [C1111,C1112,C1121,C1122][A11, A12] with input [C1111,C1122,C1112] (Hence assumes symmetry of C)
    [C1211,C1212,C1221,C1222][A21, A22]            [C2211,C2222,C2212]
    [C2111,C2112,C2121,C2122]                      [C1211,C1222,C1212]
    [C2211,C2212,C2221,C2222]
*/

// template<class E1, class E2>
// class flatprod_expr  : public _expr<flatprod_expr<E1,E2> >
// {
// public:
//     typedef typename E1::Scalar Scalar;
//     enum {ScalarValued = 0, Space = E1::Space};
//     enum {ColBlocks = 1 };

// private:
//     typename E1::Nested_t _A;
//     typename E2::Nested_t _C;
//     mutable gsMatrix<Scalar> eA, eC, tmp, tmpA, res;

// public:

//     flatprod_expr(_expr<E1> const& A, _expr<E2> const& C) : _A(A),_C(C)
//     {
//         //GISMO_ASSERT( _u.rows()*_u.cols() == _n*_m, "Wrong dimension"); //
//     }

//     const gsMatrix<Scalar> & eval(const index_t k) const
//     {
//         const index_t An = _A.cardinality();

//         eA = _A.eval(k);
//         eC = _C.eval(k);

//         // TO DO
//         // GISMO_ASSERT(Bc==_A.rows(), "Dimensions: "<<Bc<<","<< _A.rows()<< "do not match");
//         // GISMO_ASSERT(_A.rowSpan(), "First entry should be rowSpan");
//         // GISMO_ASSERT(_C.colSpan(), "Second entry should be colSpan.");

//         res.resize(eA.rows(), eA.cols());
//         tmp.resize(2,2);
//         for (index_t i = 0; i!=An; ++i) // for all actives u
//         {
//             tmpA = eA.block(0,2*i,2,2);
//             //         C1111  *A11       + C1122  *A22       + C1112  *(A12       + A21    )
//             tmp(0,0) = eC(0,0)*tmpA(0,0) + eC(0,1)*tmpA(1,1) + eC(0,2)*(tmpA(0,1) + tmpA(1,0));
//             //         C2211  *A11       + C2222  *A22       + C2212  *(A12       + A21    )
//             tmp(1,1) = eC(1,0)*tmpA(0,0) + eC(1,1)*tmpA(1,1) + eC(1,2)*(tmpA(0,1) + tmpA(1,0));
//             //         C1211  *A11       + C1222  *A22       + C1212  *(A12       + A21    )
//             tmp(0,1) = eC(2,0)*tmpA(0,0) + eC(2,1)*tmpA(1,1) + eC(2,2)*(tmpA(0,1) + tmpA(1,0));
//             //         C2111  *A11       + C2122  *A22       + C2112  *(A12       + A21    )
//             tmp(1,0) = eC(2,0)*tmpA(0,0) + eC(2,1)*tmpA(1,1) + eC(2,2)*(tmpA(0,1) + tmpA(1,0));

//             res.block(0,2*i,2,2) = tmp;
//         }
//         return res;
//     }

//     index_t rows() const { return _A.rows(); }
//     index_t cols() const { return _A.cols(); }
//     void setFlag() const { _A.setFlag();_C.setFlag(); }

//     void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
//     { _A.parse(evList);_C.parse(evList); }

//     const gsFeSpace<Scalar> & rowVar() const { return _A.rowVar(); }
//     const gsFeSpace<Scalar> & colVar() const { return _A.colVar(); }
//     index_t cardinality_impl() const { return _A.cardinality_impl(); }

//     static constexpr bool rowSpan() {return E1::rowSpan();}
//     static constexpr bool colSpan() {return E1::colSpan();}

//     void print(std::ostream &os) const { os << "flatprod_expr("; _A.print(os);_C.print(os); os<<")"; }
// };

/**
   TO ADD
 */
template<class E1, class E2, class E3>
class flatdot_expr  : public _expr<flatdot_expr<E1,E2,E3> >
{
public:
    typedef typename E1::Scalar Scalar;
    enum {ScalarValued = 0, Space = E1::Space};
private:
    typename E1::Nested_t _A;
    typename E2::Nested_t _B;
    typename E3::Nested_t _C;
    mutable gsMatrix<Scalar> eA, eB, eC, tmp, res;

public:

    flatdot_expr(_expr<E1> const& A, _expr<E2> const& B, _expr<E3> const& C) : _A(A),_B(B),_C(C)
    {
        //GISMO_ASSERT( _u.rows()*_u.cols() == _n*_m, "Wrong dimension"); //
    }

    const gsMatrix<Scalar> & eval(const index_t k) const
    {
        const index_t Ac = _A.cols();
        const index_t An = _A.cardinality();
        const index_t Bc = _B.cols();
        const index_t Bn = _B.cardinality();

        eA = _A.eval(k);
        eB = _B.eval(k);
        eC = _C.eval(k);

        // gsDebugVar(eA);
        // gsDebugVar(eB);
        // gsDebugVar(eC);

        // gsDebugVar(eA.rows());
        // gsDebugVar(eA.cols());
        // gsDebugVar(_A.rows());
        // gsDebugVar(_A.cols());
        // gsDebugVar(eB.rows());
        // gsDebugVar(eB.cols());
        // gsDebugVar(_B.rows());
        // gsDebugVar(_B.cols());

        GISMO_ASSERT(Bc==_A.rows(), "Dimensions: "<<Bc<<","<< _A.rows()<< "do not match");
        GISMO_ASSERT(_A.rowSpan(), "First entry should be rowSpan");
        GISMO_ASSERT(_B.colSpan(), "Second entry should be colSpan.");

        res.resize(An, Bn);

        // to do: evaluate the jacobians internally for the basis functions and loop over dimensions as well, since everything is same for all dimensions.
        for (index_t i = 0; i!=An; ++i) // for all actives u
            for (index_t j = 0; j!=Bn; ++j) // for all actives v
            {
                tmp.noalias() = eB.middleCols(i*Bc,Bc) * eA.middleCols(j*Ac,Ac);
                // evaluate tmp11*C11 + tmp22*C22 + (tmp12+tmp21)*C12
                // tmp(0,0) *= eC.at(0);
                // tmp(1,1) *= eC.at(1);
                // tmp(0,1) += tmp(1,0);
                // tmp(0,1) *= eC.at(1);
                // tmp(1,0) = 0.0;
                // res(i,j) = tmp.sum();

                tmp(0,0) *= eC.at(0);
                tmp(0,1) *= eC.at(2);
                tmp(1,0) *= eC.at(2);
                tmp(1,1) *= eC.at(1);
                res(i,j) = tmp.sum();
            }
        return res;
    }

    index_t rows() const { return 1; }
    index_t cols() const { return 1; }
    void setFlag() const { _A.setFlag();_B.setFlag();_C.setFlag(); }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    { _A.parse(evList);_B.parse(evList);_C.parse(evList); }

    const gsFeSpace<Scalar> & rowVar() const { return _A.rowVar(); }
    const gsFeSpace<Scalar> & colVar() const { return _B.colVar(); }
    index_t cardinality_impl() const { return _A.cardinality_impl(); }

    static constexpr bool rowSpan() {return E1::rowSpan();}
    static constexpr bool colSpan() {return E2::colSpan();}

    void print(std::ostream &os) const { os << "flatdot("; _A.print(os);_B.print(os);_C.print(os); os<<")"; }
};

/**
   To Do:
   *    Improve by inputting u instead of deriv2(u)
 */
template<class E1, class E2, class E3>
class flatdot2_expr  : public _expr<flatdot2_expr<E1,E2,E3> >
{
public:
    typedef typename E1::Scalar Scalar;
    enum {ScalarValued = 0, Space = E1::Space};
    enum {ColBlocks = 0 };

private:
    typename E1::Nested_t _A;
    typename E2::Nested_t _B;
    typename E3::Nested_t _C;
    mutable gsMatrix<Scalar> eA, eB, eC, res, tmp;

public:

    flatdot2_expr(_expr<E1> const& A, _expr<E2> const& B, _expr<E3> const& C) : _A(A),_B(B),_C(C)
    {
        //GISMO_ASSERT( _u.rows()*_u.cols() == _n*_m, "Wrong dimension"); //
    }

    const gsMatrix<Scalar> & eval(const index_t k) const
    {
        const index_t Ac = _A.cols();
        const index_t An = _A.cardinality();
        const index_t Bn = _B.cardinality();

        eA = _A.eval(k);
        eB = _B.eval(k);
        eC = _C.eval(k);

        // gsDebugVar(eA);
        // gsDebugVar(eB);
        // gsDebugVar(eC);

        // gsDebugVar(eA.rows());
        // gsDebugVar(eA.cols());
        // gsDebugVar(_A.rows());
        // gsDebugVar(_A.cols());
        // gsDebugVar(eB.rows());
        // gsDebugVar(eB.cols());
        // gsDebugVar(_B.rows());
        // gsDebugVar(_B.cols());

        GISMO_ASSERT(_B.rows()==_A.cols(), "Dimensions: "<<_B.rows()<<","<< _A.cols()<< "do not match");
        GISMO_ASSERT(_A.rowSpan(), "First entry should be rowSpan");
        GISMO_ASSERT(_B.colSpan(), "Second entry should be colSpan.");
        GISMO_ASSERT(_C.cols()==_B.rows(), "Dimensions: "<<_C.rows()<<","<< _B.rows()<< "do not match");

        // NOTE: product moved to the loop since that is more consistent with the formulations
        // for (index_t i = 0; i!=An; ++i)
        //     for (index_t j = 0; j!=Ac; ++j)
        //         eA.middleCols(i*Ac,Ac).row(j) *= eC(j);

        res.resize(An, Bn);
        for (index_t i = 0; i!=An; ++i)
            for (index_t j = 0; j!=Bn; ++j)
            {
                tmp = eA.middleCols(i*Ac,Ac) * eB.col(j);   // E_f_der2
                tmp.row(2) *= 2.0;                          // multiply the third row of E_f_der2 by 2 for voight notation
                res(i,j) = eC.row(0) * tmp.col(0);          // E_f^T * mm * E_f_der2
            }
        return res;
    }

    index_t rows() const { return 1; }
    index_t cols() const { return 1; }
    void setFlag() const { _A.setFlag();_B.setFlag();_C.setFlag(); }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    { _A.parse(evList);_B.parse(evList);_C.parse(evList); }

    const gsFeSpace<Scalar> & rowVar() const { return _A.rowVar(); }
    const gsFeSpace<Scalar> & colVar() const { return _B.colVar(); }
    index_t cardinality_impl() const { return _A.cardinality_impl(); }

    static constexpr bool rowSpan() {return E1::rowSpan();}
    static constexpr bool colSpan() {return E2::colSpan();}

    void print(std::ostream &os) const { os << "flatdot2("; _A.print(os);_B.print(os);_C.print(os); os<<")"; }
};

/*
   Expression for the transformation matrix FROM local covariant TO local cartesian bases, based on a geometry map
 */
template<class T> class cartcovinv_expr ;

template<class T>
class cartcov_expr : public _expr<cartcov_expr<T> >
{
    typename gsGeometryMap<T>::Nested_t _G;

public:
    typedef T Scalar;

    cartcov_expr(const gsGeometryMap<T> & G) : _G(G) { }

    mutable gsMatrix<Scalar,3,3> covBasis, conBasis, covMetric, conMetric, cartBasis, result;
    mutable gsVector<Scalar,3> normal, tmp;
    mutable gsVector<Scalar,3> e1, e2, a1, a2;

    MatExprType eval(const index_t k) const
    {
        // Compute covariant bases in deformed and undeformed configuration
        normal = _G.data().normals.col(k);
        normal.normalize();
        covBasis.leftCols(2) = _G.data().jacobian(k);
        covBasis.col(2)      = normal;
        covMetric = covBasis.transpose() * covBasis;

        conMetric = covMetric.inverse();

        // conBasis.col(0) = conMetric(0,0)*covBasis.col(0)+conMetric(0,1)*covBasis.col(1)+conMetric(0,2)*covBasis.col(2);
        conBasis.col(1) = conMetric(1,0)*covBasis.col(0)+conMetric(1,1)*covBasis.col(1)+conMetric(1,2)*covBasis.col(2);
        // conBasis.col(2) = conMetric(2,0)*covBasis.col(0)+conMetric(2,1)*covBasis.col(1)+conMetric(2,2)*covBasis.col(2);

        e1 = covBasis.col(0); e1.normalize();
        e2 = conBasis.col(1); e2.normalize();
        // e3 = normal;

        a1 = covBasis.col(0);
        a2 = covBasis.col(1);

        result(0,0) = (e1.dot(a1))*(a1.dot(e1));
        result(0,1) = (e1.dot(a2))*(a2.dot(e2));
        result(0,2) = 2*(e1.dot(a1))*(a2.dot(e1));
        // Row 1
        result(1,0) = (e2.dot(a1))*(a1.dot(e2));
        result(1,1) = (e2.dot(a2))*(a2.dot(e2));
        result(1,2) = 2*(e2.dot(a1))*(a2.dot(e2));
        // Row 2
        result(2,0) = (e1.dot(a1))*(a1.dot(e2));
        result(2,1) = (e1.dot(a2))*(a2.dot(e2));
        result(2,2) = (e1.dot(a1))*(a2.dot(e2)) + (e1.dot(a2))*(a1.dot(e2));

        // return result.inverse(); // !!!!
        return result;
    }

    cartcovinv_expr<T> inv() const
    {
        GISMO_ASSERT(rows() == cols(), "The Jacobian matrix is not square");
        return cartcovinv_expr<T>(_G);
    }

    index_t rows() const { return 3; }

    index_t cols() const { return 3; }

    static constexpr bool rowSpan() {return false; }
    static bool colSpan() {return false;}

    static const gsFeSpace<Scalar> & rowVar() { return gsNullExpr<Scalar>::get(); }
    static const gsFeSpace<Scalar> & colVar() { return gsNullExpr<Scalar>::get(); }

    void setFlag() const { _G.data().flags |= NEED_NORMAL|NEED_DERIV; }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    {
        //GISMO_ASSERT(NULL!=m_fd, "FeVariable: FuncData member not registered");
        evList.push_sorted_unique(&_G.source());
        _G.data().flags |= NEED_NORMAL|NEED_DERIV;
    }

    void print(std::ostream &os) const { os << "cartcov("; _G.print(os); os <<")"; }
};

template<class T>
class cartcovinv_expr : public _expr<cartcovinv_expr<T> >
{
    typename gsGeometryMap<T>::Nested_t _G;

public:
    typedef T Scalar;

    cartcovinv_expr(const gsGeometryMap<T> & G) : _G(G) { }

    mutable gsMatrix<T> temp;

    MatExprType eval(const index_t k) const
    {
        return  (cartcov_expr(_G).eval(k)).reshape(3,3).inverse();
    }

    index_t rows() const { return 3; }

    index_t cols() const { return 3; }

    static constexpr bool rowSpan() {return false; }
    static bool colSpan() {return false;}

    static const gsFeSpace<Scalar> & rowVar() { return gsNullExpr<Scalar>::get(); }
    static const gsFeSpace<Scalar> & colVar() { return gsNullExpr<Scalar>::get(); }

    void setFlag() const { _G.data().flags |= NEED_NORMAL|NEED_DERIV; }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    {
        //GISMO_ASSERT(NULL!=m_fd, "FeVariable: FuncData member not registered");
        evList.push_sorted_unique(&_G.source());
        _G.data().flags |= NEED_NORMAL|NEED_DERIV;
    }

    void print(std::ostream &os) const { os << "cartcovinv("; _G.print(os); os <<")"; }
};


/*
   Expression for the transformation matrix FROM local contravariant TO local cartesian bases, based on a geometry map
 */
template<class T> class cartconinv_expr ;

template<class T>
class cartcon_expr : public _expr<cartcon_expr<T> >
{
    typename gsGeometryMap<T>::Nested_t _G;

public:
    typedef T Scalar;

    cartcon_expr(const gsGeometryMap<T> & G) : _G(G) { }

    mutable gsMatrix<Scalar,3,3> covBasis, conBasis, covMetric, conMetric, cartBasis, result;
    mutable gsVector<Scalar,3> normal, tmp;
    mutable gsVector<Scalar,3> e1, e2, ac1, ac2;

    MatExprType eval(const index_t k) const
    {
        // Compute covariant bases in deformed and undeformed configuration
        normal = _G.data().normals.col(k);
        normal.normalize();
        covBasis.leftCols(2) = _G.data().jacobian(k);
        covBasis.col(2)      = normal;
        covMetric = covBasis.transpose() * covBasis;

        conMetric = covMetric.inverse();

        conBasis.col(0) = conMetric(0,0)*covBasis.col(0)+conMetric(0,1)*covBasis.col(1)+conMetric(0,2)*covBasis.col(2);
        conBasis.col(1) = conMetric(1,0)*covBasis.col(0)+conMetric(1,1)*covBasis.col(1)+conMetric(1,2)*covBasis.col(2);
        // conBasis.col(2) = conMetric(2,0)*covBasis.col(0)+conMetric(2,1)*covBasis.col(1)+conMetric(2,2)*covBasis.col(2);

        e1 = covBasis.col(0); e1.normalize();
        e2 = conBasis.col(1); e2.normalize();
        // e3 = normal;

        ac1 = conBasis.col(0);
        ac2 = conBasis.col(1);

        result(0,0) = (e1.dot(ac1))*(ac1.dot(e1));
        result(0,1) = (e1.dot(ac2))*(ac2.dot(e2));
        result(0,2) = 2*(e1.dot(ac1))*(ac2.dot(e1));
        // Row 1
        result(1,0) = (e2.dot(ac1))*(ac1.dot(e2));
        result(1,1) = (e2.dot(ac2))*(ac2.dot(e2));
        result(1,2) = 2*(e2.dot(ac1))*(ac2.dot(e2));
        // Row 2
        result(2,0) = (e1.dot(ac1))*(ac1.dot(e2));
        result(2,1) = (e1.dot(ac2))*(ac2.dot(e2));
        result(2,2) = (e1.dot(ac1))*(ac2.dot(e2)) + (e1.dot(ac2))*(ac1.dot(e2));

        return result;
    }

    cartconinv_expr<T> inv() const
    {
        GISMO_ASSERT(rows() == cols(), "The Jacobian matrix is not square");
        return cartconinv_expr<T>(_G);
    }

    index_t rows() const { return 3; }

    index_t cols() const { return 3; }

    static constexpr bool rowSpan() {return false; }
    static bool colSpan() {return false;}

    static const gsFeSpace<Scalar> & rowVar() { return gsNullExpr<Scalar>::get(); }
    static const gsFeSpace<Scalar> & colVar() { return gsNullExpr<Scalar>::get(); }

    void setFlag() const { _G.data().flags |= NEED_NORMAL|NEED_DERIV; }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    {
        //GISMO_ASSERT(NULL!=m_fd, "FeVariable: FuncData member not registered");
        evList.push_sorted_unique(&_G.source());
        _G.data().flags |= NEED_NORMAL|NEED_DERIV;
    }

    void print(std::ostream &os) const { os << "cartcon("; _G.print(os); os <<")"; }
};

template<class T>
class cartconinv_expr : public _expr<cartconinv_expr<T> >
{
    typename gsGeometryMap<T>::Nested_t _G;

public:
    typedef T Scalar;

    cartconinv_expr(const gsGeometryMap<T> & G) : _G(G) { }

    mutable gsMatrix<T> temp;

    MatExprType eval(const index_t k) const
    {
        return  (cartcon_expr(_G).eval(k)).reshape(3,3).inverse();
    }

    index_t rows() const { return 3; }

    index_t cols() const { return 3; }

    static constexpr bool rowSpan() {return false; }
    static bool colSpan() {return false;}

    static const gsFeSpace<Scalar> & rowVar() { return gsNullExpr<Scalar>::get(); }
    static const gsFeSpace<Scalar> & colVar() { return gsNullExpr<Scalar>::get(); }

    void setFlag() const { _G.data().flags |= NEED_NORMAL|NEED_DERIV; }

    void parse(gsSortedVector<const gsFunctionSet<Scalar>*> & evList) const
    {
        //GISMO_ASSERT(NULL!=m_fd, "FeVariable: FuncData member not registered");
        evList.push_sorted_unique(&_G.source());
        _G.data().flags |= NEED_NORMAL|NEED_DERIV;
    }

    void print(std::ostream &os) const { os << "cartconinv("; _G.print(os); os <<")"; }
};

template<class E> EIGEN_STRONG_INLINE
var1_expr<E> var1(const E & u, const gsGeometryMap<typename E::Scalar> & G) { return var1_expr<E>(u, G); }

template<class E1, class E2, class E3> EIGEN_STRONG_INLINE
var2_expr<E1,E2,E3> var2(const E1 & u, const E2 & v, const gsGeometryMap<typename E1::Scalar> & G, const E3 & Ef)
{ return var2_expr<E1,E2,E3>(u,v, G, Ef); }

// template<class E1, class E2> EIGEN_STRONG_INLINE
// hessdot_expr<E1,E2> hessdot(const E1 & u, const E2 & v) { return hessdot_expr<E1,E2>(u, v); }

template<class E> EIGEN_STRONG_INLINE
deriv2_expr<E> deriv2(const E & u) { return deriv2_expr<E>(u); }

template<class E1, class E2> EIGEN_STRONG_INLINE
deriv2dot_expr<E1, E2> deriv2(const E1 & u, const E2 & v) { return deriv2dot_expr<E1, E2>(u,v); }

// template<class E1, class E2> EIGEN_STRONG_INLINE
// flatprod_expr<E1,E2> flatprod(const E1 & u, const E2 & v)
// { return flatprod_expr<E1,E2>(u, v); }

template<class E1, class E2, class E3> EIGEN_STRONG_INLINE
flatdot_expr<E1,E2,E3> flatdot(const E1 & u, const E2 & v, const E3 & w)
{ return flatdot_expr<E1,E2,E3>(u, v, w); }

template<class E1, class E2, class E3> EIGEN_STRONG_INLINE
flatdot2_expr<E1,E2,E3> flatdot2(const E1 & u, const E2 & v, const E3 & w)
{ return flatdot2_expr<E1,E2,E3>(u, v, w); }

template<class E> EIGEN_STRONG_INLINE
cartcov_expr<E> cartcov(const gsGeometryMap<E> & G) { return cartcov_expr<E>(G); }

template<class E> EIGEN_STRONG_INLINE
cartcon_expr<E> cartcon(const gsGeometryMap<E> & G) { return cartcon_expr<E>(G); }

}
}


using namespace gismo;

template <class T>
void evaluateFunction(gsExprEvaluator<T> ev, auto expression, gsVector<T> pt);
template <class T>
void evaluateFunction(gsExprEvaluator<T> ev, auto expression, gsMatrix<T> pt);
template <class T>
void constructSolution(gsExprAssembler<T> assembler, gsMultiPatch<T> mp_def);

// To Do:
// * struct for material model
// Input is parametric coordinates of the surface \a mp
template <class T>
class gsMaterialMatrix : public gismo::gsFunction<T>
{
  // Computes the material matrix for different material models
  //
protected:
    const gsFunctionSet<T> * _mp;
    const gsFunction<T> * _YoungsModulus;
    const gsFunction<T> * _PoissonRatio;
    mutable gsMapData<T> _tmp;
    mutable gsMatrix<real_t,3,3> F0;
    mutable gsMatrix<T> Emat,Nmat;
    mutable real_t lambda, mu, E, nu, C_constant;

public:
    /// Shared pointer for gsMaterialMatrix
    typedef memory::shared_ptr< gsMaterialMatrix > Ptr;

    /// Unique pointer for gsMaterialMatrix
    typedef memory::unique_ptr< gsMaterialMatrix > uPtr;

    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    gsMaterialMatrix(const gsFunctionSet<T> & mp, const gsFunction<T> & YoungsModulus,
                   const gsFunction<T> & PoissonRatio) :
    _mp(&mp), _YoungsModulus(&YoungsModulus), _PoissonRatio(&PoissonRatio), _mm_piece(nullptr)
    {
        _tmp.flags = NEED_JACOBIAN | NEED_NORMAL | NEED_VALUE;
    }

    ~gsMaterialMatrix() { delete _mm_piece; }

    GISMO_CLONE_FUNCTION(gsMaterialMatrix)

    short_t domainDim() const {return 2;}

    short_t targetDim() const {return 9;}

    mutable gsMaterialMatrix<T> * _mm_piece; // todo: improve the way pieces are accessed

    const gsFunction<T> & piece(const index_t k) const
    {
        delete _mm_piece;
        _mm_piece = new gsMaterialMatrix(_mp->piece(k), *_YoungsModulus, *_PoissonRatio);
        return *_mm_piece;
    }

    //class .. matMatrix_z
    // should contain eval_into(thickness variable)

    // Input is parametric coordinates of the surface \a mp
    void eval_into(const gsMatrix<T>& u, gsMatrix<T>& result) const
    {
        // NOTE 1: if the input \a u is considered to be in physical coordinates
        // then we first need to invert the points to parameter space
        // _mp.patch(0).invertPoints(u, _tmp.points, 1e-8)
        // otherwise we just use the input paramteric points
        _tmp.points = u;

        static_cast<const gsFunction<T>&>(_mp->piece(0)).computeMap(_tmp); // the piece(0) here implies that if you call class.eval_into, it will be evaluated on piece(0). Hence, call class.piece(k).eval_into()

        // NOTE 2: in the case that parametric value is needed it suffices
        // to evaluate Youngs modulus and Poisson's ratio at
        // \a u instead of _tmp.values[0].
        _YoungsModulus->eval_into(_tmp.values[0], Emat);
        _PoissonRatio->eval_into(_tmp.values[0], Nmat);

        result.resize( targetDim() , u.cols() );
        for( index_t i=0; i< u.cols(); ++i )
        {
            gsAsMatrix<T, Dynamic, Dynamic> C = result.reshapeCol(i,3,3);

            F0.leftCols(2) = _tmp.jacobian(i);
            F0.col(2)      = _tmp.normal(i).normalized();
            F0 = F0.inverse();
            F0 = F0 * F0.transpose(); //3x3

            // Evaluate material properties on the quadrature point
            E = Emat(0,i);
            nu = Nmat(0,i);
            lambda = E * nu / ( (1. + nu)*(1.-2.*nu)) ;
            mu     = E / (2.*(1. + nu)) ;

            C_constant = 2*lambda*mu/(lambda+2*mu);

            C(0,0) = C_constant*F0(0,0)*F0(0,0) + 1*mu*(2*F0(0,0)*F0(0,0));
            C(1,1) = C_constant*F0(1,1)*F0(1,1) + 1*mu*(2*F0(1,1)*F0(1,1));
            C(2,2) = C_constant*F0(0,1)*F0(0,1) + 1*mu*(F0(0,0)*F0(1,1) + F0(0,1)*F0(0,1));
            C(1,0) =
            C(0,1) = C_constant*F0(0,0)*F0(1,1) + 1*mu*(2*F0(0,1)*F0(0,1));
            C(2,0) =
            C(0,2) = C_constant*F0(0,0)*F0(0,1) + 1*mu*(2*F0(0,0)*F0(0,1));
            C(2,1) = C(1,2) = C_constant*F0(0,1)*F0(1,1) + 1*mu*(2*F0(0,1)*F0(1,1));

            //gsDebugVar(C);
        }
    }

    // piece(k) --> for patch k

}; //! [Include namespace]

template <class T>
class gsMaterialMatrixD : public gismo::gsFunction<T>
{
  // Computes the material matrix for different material models
  //
protected:
    const gsFunctionSet<T> * _mp;
    const gsFunction<T> * _YoungsModulus;
    const gsFunction<T> * _PoissonRatio;
    mutable gsMapData<T> _tmp;
    mutable gsMatrix<T> Emat,Nmat;
    mutable real_t E, nu, D_constant;

public:
    /// Shared pointer for gsMaterialMatrixD
    typedef memory::shared_ptr< gsMaterialMatrixD > Ptr;

    /// Unique pointer for gsMaterialMatrixD
    typedef memory::unique_ptr< gsMaterialMatrixD > uPtr;

    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    gsMaterialMatrixD(const gsFunctionSet<T> & mp, const gsFunction<T> & YoungsModulus,
                   const gsFunction<T> & PoissonRatio) :
    _mp(&mp), _YoungsModulus(&YoungsModulus), _PoissonRatio(&PoissonRatio), _mm_piece(nullptr)
    {
        _tmp.flags = NEED_JACOBIAN | NEED_NORMAL | NEED_VALUE;
    }

    ~gsMaterialMatrixD() { delete _mm_piece; }

    GISMO_CLONE_FUNCTION(gsMaterialMatrixD)

    short_t domainDim() const {return 2;}

    short_t targetDim() const {return 9;}

    mutable gsMaterialMatrixD<T> * _mm_piece; // todo: improve the way pieces are accessed

    const gsFunction<T> & piece(const index_t k) const
    {
        delete _mm_piece;
        _mm_piece = new gsMaterialMatrixD(_mp->piece(k), *_YoungsModulus, *_PoissonRatio);
        return *_mm_piece;
    }

    //class .. matMatrix_z
    // should contain eval_into(thickness variable)

    // Input is parametric coordinates of the surface \a mp
    void eval_into(const gsMatrix<T>& u, gsMatrix<T>& result) const
    {
        // NOTE 1: if the input \a u is considered to be in physical coordinates
        // then we first need to invert the points to parameter space
        // _mp.patch(0).invertPoints(u, _tmp.points, 1e-8)
        // otherwise we just use the input paramteric points
        _tmp.points = u;

        static_cast<const gsFunction<T>*>(_mp)->computeMap(_tmp);

        // NOTE 2: in the case that parametric value is needed it suffices
        // to evaluate Youngs modulus and Poisson's ratio at
        // \a u instead of _tmp.values[0].
        _YoungsModulus->eval_into(_tmp.values[0], Emat);
        _PoissonRatio->eval_into(_tmp.values[0], Nmat);

        result.resize( targetDim() , u.cols() );
        for( index_t i=0; i< u.cols(); ++i )
        {
            gsAsMatrix<T, Dynamic, Dynamic> D = result.reshapeCol(i,3,3);

            // Evaluate material properties on the quadrature point
            E = Emat(0,i);
            nu = Nmat(0,i);

            D_constant = E/(1-nu*nu);
            D(0,0) = D(1,1) = 1;
            D(2,2) = (1-nu)/2;
            D(0,1) = D(1,0) = nu;
            D(2,0) = D(0,2) = D(2,1) = D(1,2) = 0;
            D *= D_constant;

            //gsDebugVar(C);
        }
    }

    // piece(k) --> for patch k

}; //! [Include namespace]

/*
    Todo:
        * Improve for mu, E, phi as gsFunction instead of reals!!
*/
template <class T, int mat>
class gsMaterialMatrixComp : public gismo::gsFunction<T>
{
  // Computes the material matrix for different material models
  // NOTE: This material matrix is in local Cartesian coordinates and should be transformed!
  // NOTE: To make this efficient, we can output all matrices stacked and use expressions to pick the 1st, second and third
protected:
    // const gsFunctionSet<T> * _mp;
    const std::vector<std::pair<T,T>> _YoungsModuli;
    const std::vector<T> _ShearModuli;
    const std::vector<std::pair<T,T>> _PoissonRatios;
    const std::vector<T> _thickness;
    const std::vector<T> _phi;
    mutable gsMapData<T> _tmp;
    mutable real_t E1, E2, G12, nu12, nu21, t, t_tot, t_temp, z, z_mid, phi;
    mutable gsMatrix<T> Tmat, Dmat;

public:
    /// Shared pointer for gsMaterialMatrixComp
    typedef memory::shared_ptr< gsMaterialMatrixComp > Ptr;

    /// Unique pointer for gsMaterialMatrixComp
    typedef memory::unique_ptr< gsMaterialMatrixComp > uPtr;

    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    gsMaterialMatrixComp(  //const gsFunctionSet<T> & mp,
                        const std::vector<std::pair<T,T>> & YoungsModuli,
                        const std::vector<T> & ShearModuli,
                        const std::vector<std::pair<T,T>> & PoissonRatios,
                        const std::vector<T> thickness,
                        const std::vector<T> phi) :
    // _mp(&mp),
    _YoungsModuli(YoungsModuli),
    _ShearModuli(ShearModuli),
    _PoissonRatios(PoissonRatios),
    _thickness(thickness),
    _phi(phi)//,
    // _mm_piece(nullptr)
    {
        _tmp.flags = NEED_JACOBIAN | NEED_NORMAL | NEED_VALUE;
    }

    // ~gsMaterialMatrixComp() { delete _mm_piece; }

    GISMO_CLONE_FUNCTION(gsMaterialMatrixComp)

    short_t domainDim() const {return 2;}

    short_t targetDim() const {return 9;}

    // mutable gsMaterialMatrixComp<T> * _mm_piece; // todo: improve the way pieces are accessed

    // const gsFunction<T> & piece(const index_t k) const
    // {
    //     delete _mm_piece;
    //     _mm_piece = new gsMaterialMatrixComp(_mp->piece(k), _YoungsModuli, _ShearModuli, _PoissonRatios, _thickness, _phi);
    //     return *_mm_piece;
    // }

    // Input is parametric coordinates of the surface \a mp
    void eval_into(const gsMatrix<T>& u, gsMatrix<T>& result) const
    {
        // static_cast<const gsFunction<T>*>(_mp)->computeMap(_tmp);
        GISMO_ASSERT(_YoungsModuli.size()==_PoissonRatios.size(),"Size of vectors of Youngs Moduli and Poisson Ratios is not equal: " << _YoungsModuli.size()<<" & "<<_PoissonRatios.size());
        GISMO_ASSERT(_YoungsModuli.size()==_ShearModuli.size(),"Size of vectors of Youngs Moduli and Shear Moduli is not equal: " << _YoungsModuli.size()<<" & "<<_ShearModuli.size());
        GISMO_ASSERT(_thickness.size()==_phi.size(),"Size of vectors of thickness and angles is not equal: " << _thickness.size()<<" & "<<_phi.size());
        GISMO_ASSERT(_YoungsModuli.size()==_thickness.size(),"Size of vectors of material properties and laminate properties is not equal: " << _YoungsModuli.size()<<" & "<<_thickness.size());
        GISMO_ASSERT(_YoungsModuli.size()!=0,"No laminates defined");

        // Compute total thickness (sum of entries)
        t_tot = std::accumulate(_thickness.begin(), _thickness.end(), 0.0);

        // compute mid-plane height of total plate
        z_mid = t_tot / 2.0;

        // now we use t_temp to add the thickness of all plies iteratively
        t_temp = 0.0;

        // Initialize material matrix and result
        Dmat.resize(3,3);
        result.resize( targetDim(), 1 );

        // Initialize transformation matrix
        Tmat.resize(3,3);


        for (size_t i = 0; i != _phi.size(); ++i) // loop over laminates
        {
            // Compute all quantities
            E1 = _YoungsModuli[i].first;
            E2 = _YoungsModuli[i].second;
            G12 = _ShearModuli[i];
            nu12 = _PoissonRatios[i].first;
            nu21 = _PoissonRatios[i].second;
            t = _thickness[i];
            phi = _phi[i];

            GISMO_ASSERT(nu21*E1 == nu12*E2, "No symmetry in material properties for ply "<<i<<". nu12*E2!=nu21*E1:\n"<<
                    "\tnu12 = "<<nu12<<"\t E2 = "<<E2<<"\t nu12*E2 = "<<nu12*E2<<"\n"
                  <<"\tnu21 = "<<nu21<<"\t E1 = "<<E1<<"\t nu21*E1 = "<<nu21*E1);

            // Fill material matrix
            Dmat(0,0) = E1 / (1-nu12*nu21);
            Dmat(1,1) = E2 / (1-nu12*nu21);;
            Dmat(2,2) = G12;
            Dmat(0,1) = nu21*E1 / (1-nu12*nu21);
            Dmat(1,0) = nu12*E2 / (1-nu12*nu21);
            Dmat(2,0) = Dmat(0,2) = Dmat(2,1) = Dmat(1,2) = 0.0;

            // Make transformation matrix
            Tmat(0,0) = Tmat(1,1) = math::pow(math::cos(phi),2);
            Tmat(0,1) = Tmat(1,0) = math::pow(math::sin(phi),2);
            Tmat(2,0) = Tmat(0,2) = Tmat(2,1) = Tmat(1,2) = math::sin(phi) * math::cos(phi);
            Tmat(2,0) *= -2.0;
            Tmat(2,1) *= 2.0;
            Tmat(1,2) *= -1.0;
            Tmat(2,2) = math::pow(math::cos(phi),2) - math::pow(math::sin(phi),2);

            // Compute laminate stiffness matrix
            Dmat = Tmat.transpose() * Dmat * Tmat;

            z = math::abs(z_mid - (t/2.0 + t_temp) ); // distance from mid-plane of plate

            // Make matrices A, B and C
            // [NOTE: HOW TO DO THIS NICELY??]
            // result.reshape(3,3) += Dmat * t; // A
            // result.reshape(3,3) += Dmat * t*z; // B
            // result.reshape(3,3) += Dmat * ( t*z*z + t*t*t/12.0 ); // D
            switch (mat)
            {
                case 0:
                    result.reshape(3,3) += Dmat * t; // A
                    break;
                case 1:
                    result.reshape(3,3) += Dmat * t; // A
                    break;
                case 2:
                    result.reshape(3,3) += Dmat * t; // A
                    break;
            }

            t_temp += t;
        }

        GISMO_ASSERT(t_tot==t_temp,"Total thickness after loop is wrong. t_temp = "<<t_temp<<" and sum(thickness) = "<<t_tot);

        // Replicate for all points since the quantities are equal over the whole domain
        result.replicate(1, u.cols());
    }

    // piece(k) --> for patch k

};

template <class T>
gsMultiPatch<T> RectangularDomain(int n, int m, int p, int q, T L, T B);
template <class T>
gsMultiPatch<T> RectangularDomain(int n, int p, T L, T B);

int main(int argc, char *argv[])
{
    //! [Parse command line]
    bool plot = false;
    index_t numRefine  = 1;
    index_t numElevate = 1;
    index_t testCase = 1;
    bool nonlinear = false;
    std::string fn("pde/poisson2d_bvp.xml");

    real_t E_modulus = 1.0;
    real_t PoissonRatio = 0.0;
    real_t thickness = 1.0;

    index_t MaterialLaw = 0;
    index_t Compressibility = 0;

    gsCmdLine cmd("Tutorial on solving a Poisson problem.");
    cmd.addInt( "e", "degreeElevation",
                "Number of degree elevation steps to perform before solving (0: equalize degree in all directions)", numElevate );
    cmd.addInt( "r", "uniformRefine", "Number of Uniform h-refinement steps to perform before solving",  numRefine );
    cmd.addInt( "t", "testCase", "Test case to run: 1 = unit square; 2 = Scordelis Lo Roof",  testCase );
    cmd.addInt( "l", "law", "MaterialLaw", MaterialLaw );
    cmd.addInt( "c", "comp", "Compressibility", Compressibility );
    cmd.addString( "f", "file", "Input XML file", fn );
    cmd.addSwitch("nl", "Solve nonlinear problem", nonlinear);
    cmd.addSwitch("plot", "Create a ParaView visualization file with the solution", plot);

    try { cmd.getValues(argc,argv); } catch (int rv) { return rv; }
    //! [Parse command line]

    //! [Read input file]
    gsMultiPatch<> mp;
    gsMultiPatch<> mp_def;

    if (testCase == 2  || testCase == 3)
    {
        thickness = 0.25;
        E_modulus = 4.32E8;
        fn = "../extensions/unsupported/filedata/scordelis_lo_roof.xml";
        gsReadFile<>(fn, mp);
        PoissonRatio = 0.0;
    }
    else if (testCase == 6)
    {
        thickness = 0.1;
        real_t mu = 4.225e5;
        PoissonRatio = 0.3;
        E_modulus = (2+PoissonRatio)*mu;
        fn = "../extensions/unsupported/filedata/quarter_sphere.xml";
        gsReadFile<>(fn, mp);
    }
    else
    {
        // Unit square
        mp.addPatch( gsNurbsCreator<>::BSplineSquare(1) ); // degree
        mp.addAutoBoundaries();
        mp.embed(3);
        E_modulus = 1e0;
        thickness = 1e0;
        PoissonRatio = 0.0;
        // PoissonRatio = 0.499;
    }
    //! [Read input file]

    // p-refine
    if (numElevate!=0)
        mp.degreeElevate(numElevate);

    // h-refine
    for (int r =0; r < numRefine; ++r)
        mp.uniformRefine();

    mp_def = mp;

    //! [Refinement]
    gsMultiBasis<> dbasis(mp);

    gsInfo << "Patches: "<< mp.nPatches() <<", degree: "<< dbasis.minCwiseDegree() <<"\n";
    gsInfo << dbasis.basis(0)<<"\n";

    gsBoundaryConditions<> bc;
    bc.setGeoMap(mp);
    gsVector<> tmp(3);
    tmp << 0, 0, 0;

    gsVector<> neu(3);
    neu << 0, 0, 0;

    gsFunctionExpr<> displ("1",3);

    gsConstantFunction<> neuData(neu,3);

    real_t pressure = 0.0;
    if (testCase == 1)
    {
        for (index_t i=0; i!=3; ++i)
        {
            bc.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, i ); // unknown 0 - x
            bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, i ); // unknown 1 - y
            bc.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
            bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
        }
        // tmp << 0,0,0;
        tmp << 0,0,-1;

        // Point loads
        // gsVector<> point(2);
        // gsVector<> load (3);
        // point<< 0.5, 0.5 ; load << 0.0, 1.0, 0.0 ;
        // pLoads.addLoad(point, load, 0 );
    }
    else if (testCase == 2)
    {
        // Diaphragm conditions
        bc.addCondition(boundary::west, condition_type::dirichlet, 0, 1 ); // unknown 1 - y
        bc.addCondition(boundary::west, condition_type::dirichlet, 0, 2 ); // unknown 2 - z

        // ORIGINAL
        bc.addCornerValue(boundary::southwest, 0.0, 0, 0); // (corner,value, patch, unknown)

        bc.addCondition(boundary::east, condition_type::dirichlet, 0, 1 ); // unknown 1 - y
        bc.addCondition(boundary::east, condition_type::dirichlet, 0, 2 ); // unknown 2 - z

        // NOT ORIGINAL
        // bc.addCondition(boundary::west, condition_type::dirichlet, &displ, 0 ); // unknown 1 - x
        // bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0 ); // unknown 1 - x
        // bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0 ); // unknown 1 - x

        // Surface forces
        tmp << 0, 0, -90;

        // for (index_t i=0; i!=3; ++i)
        // {
        //     bc.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, i ); // unknown 0 - x
        //     bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, i ); // unknown 1 - y
        //     bc.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
        //     bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
        // }
        // tmp << 0,0,-90;
    }
    else if (testCase == 3)
    {
        neu << 0, 0, -90;
        neuData.setValue(neu,3);
        // Diaphragm conditions
        // bc.addCondition(boundary::west, condition_type::dirichlet, 0, 1 ); // unknown 1 - y
        // bc.addCondition(boundary::west, condition_type::dirichlet, 0, 2 ); // unknown 2 - z
        bc.addCondition(boundary::west, condition_type::neumann, &neuData );

        // ORIGINAL
        // bc.addCornerValue(boundary::southwest, 0.0, 0, 0); // (corner,value, patch, unknown)

        bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, 1 ); // unknown 1 - y
        bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, 2 ); // unknown 2 - z

        // NOT ORIGINAL
        // bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0 ); // unknown 1 - x
        bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, 0 ); // unknown 1 - x

        // Surface forces
        tmp << 0, 0, 0;
    }
    else if (testCase == 4)
    {
        for (index_t i=0; i!=3; ++i)
        {
            bc.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, i ); // unknown 0 - x
            bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, i ); // unknown 1 - y
            bc.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
            bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
        }
        pressure = -1.0;
    }

    else if (testCase == 6)
    {
        // bc.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, 0 ); // unknown 0 - x
        // bc.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, 1 ); // unknown 1 - y
        // bc.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, 2 ); // unknown 2 - z

        // bc.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 0 ); // unknown 0 - x
        // bc.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 1 ); // unknown 1 - y
        bc.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, 2 ); // unknown 2 - z

        // Symmetry in x-direction:
        bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, 0 );
        bc.addCondition(boundary::east, condition_type::clamped, 0, 0, false, 1 );
        bc.addCondition(boundary::east, condition_type::clamped, 0, 0, false, 2 );

        // Symmetry in y-direction:
        bc.addCondition(boundary::west, condition_type::clamped, 0, 0, false, 0 );
        bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, 1 );
        bc.addCondition(boundary::west, condition_type::clamped, 0, 0, false, 2 );

        // Pressure
        pressure = 5e3;
    }

    else if (testCase == 10)
    {
        bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, 0 ); // unknown 0 - x
        bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, 1 ); // unknown 1 - y
        bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, 2 ); // unknown 2 - z
        bc.addCondition(boundary::east, condition_type::dirichlet, &displ, 0, false, 1 ); // unknown 0 - x

        // Surface forces
        tmp.setZero();
    }
    else if (testCase == 11)
    {
        for (index_t i=0; i!=3; ++i)
        {
            bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, i ); // unknown 1 - y
            bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
        }

        bc.addCondition(boundary::east, condition_type::clamped, 0, 0 ,false,2);
        bc.addCondition(boundary::west, condition_type::clamped, 0, 0 ,false,2);

        tmp<<0,0,-1;
    }
    else if (testCase == 12)
    {
        for (index_t i=0; i!=3; ++i)
        {
            bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
        }

        bc.addCondition(boundary::west, condition_type::clamped, 0, 0 ,false,2);

        neu << 0, 0, -0.1;
        neuData.setValue(neu,3);

        bc.addCondition(boundary::east, condition_type::neumann, &neuData );
    }
    else if (testCase == 13)
    {
        for (index_t i=0; i!=3; ++i)
        {
            bc.addCondition(boundary::north, condition_type::dirichlet, 0, 0, false, i ); // unknown 0 - x
            bc.addCondition(boundary::east, condition_type::dirichlet, 0, 0, false, i ); // unknown 1 - y
            bc.addCondition(boundary::south, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
            bc.addCondition(boundary::west, condition_type::dirichlet, 0, 0, false, i ); // unknown 2 - z
        }

        bc.addCondition(boundary::north, condition_type::clamped, 0, 0 ,false,2);
        bc.addCondition(boundary::east, condition_type::clamped, 0, 0 ,false,2);
        bc.addCondition(boundary::south, condition_type::clamped, 0, 0 ,false,2);
        bc.addCondition(boundary::west, condition_type::clamped, 0, 0 ,false,2);

        tmp << 0,0,-1;
    }
    //! [Refinement]

    //! [Problem setup]
    gsExprAssembler<> A;

    //gsInfo<<"Active options:\n"<< A.options() <<"\n";
    typedef gsExprAssembler<>::geometryMap geometryMap;
    typedef gsExprAssembler<>::variable    variable;
    typedef gsExprAssembler<>::space       space;
    typedef gsExprAssembler<>::solution    solution;

    // Elements used for numerical integration
    A.setIntegrationElements(dbasis);
    gsExprEvaluator<> ev(A);

    // Set the geometry map
    geometryMap G = A.getMap(mp); // the last map counts
    geometryMap defG = A.getMap(mp_def);


    // Set the discretization space
    space u = A.getSpace(dbasis, 3);
    // u.setInterfaceCont(0); // todo: 1 (smooth basis)
    // u.addBc( bc.get("Dirichlet") ); // (!) must be called only once

    u.setup(bc, dirichlet::interpolation, 0);

    gsDebug<<u.mapper()<<"\n";

    // Solution vector and solution variable
    gsMatrix<> random;
    solution u_sol = A.getSolution(u,random);

    // gsFunctionExpr<> materialMat("1","0","0","0","1","0","0","0","1",3);
    // variable mm = A.getCoeff(materialMat, G);
    gsFunctionExpr<> E(std::to_string(E_modulus),3);
    gsFunctionExpr<> nu(std::to_string(PoissonRatio),3);
    gsMaterialMatrix materialMat(mp, E, nu);
    variable mm = A.getCoeff(materialMat); // evaluates in the parametric domain, but the class transforms E and nu to physical
    gsMaterialMatrixD materialMatD(mp, E, nu);
    variable mmD = A.getCoeff(materialMatD); // evaluates in the parametric domain, but the class transforms E and nu to physical

    gsFunctionExpr<> t(std::to_string(thickness), 3);
    variable tt = A.getCoeff(t, G); // evaluates in the physical domain

    gsFunctionExpr<> mult2t("1","0","0","0","1","0","0","0","2",3);
    variable m2 = A.getCoeff(mult2t, G); // evaluates in the physical domain

    // gsFunctionExpr<> force("0","0","1", 3);
    gsConstantFunction<> force(tmp,3);
    variable ff = A.getCoeff(force,G); // evaluates in the physical domain

    gsSparseSolver<>::CGDiagonal solver;

    //! [Problem setup]

    //! [Solver loop]

    // Set Dirichlet values
    //A.options().setInt("DirichletValues", dirichlet::homogeneous);

    // Initialize the system
    A.initSystem(false);

    gsInfo<<"Number of degrees of freedom: "<< A.numDofs() <<"\n"<<std::flush;

    /*
        We provide the following functions:                         checked with previous assembler
            E_m         membrane strain tensor.                             V
            E_m_der     first variation of E_m.                             V
            E_m_der2    second variation of E_m MULTIPLIED BY S_m.          V
            E_f         flexural strain tensor.                             V
            E_f_der     second variation of E_f.                            V
            E_f_der2    second variation of E_f MULTIPLIED BY S_f.          X NOTE: var1 in E_f_der2 in previous assembler is computed with both G and defG

        Where:
            G       the undeformed geometry,
            defG    the deformed geometry,
            mm      the material matrix,
            m2      an auxillary matrix to multiply the last row of a tensor with 2
    **/


    // Membrane components
    auto E_m = 0.5 * ( flat(jac(defG).tr()*jac(defG)) - flat(jac(G).tr()* jac(G)) ) ; //[checked]
    auto S_m = E_m * reshape(mm,3,3);
    auto N   = tt.val() * S_m;

    auto E_m_der = flat( jac(defG).tr() * jac(u) ) ; //[checked]
    auto S_m_der = E_m_der * reshape(mm,3,3);
    auto N_der   = tt.val() * S_m_der;

    auto E_m_der2 = flatdot( jac(u),jac(u).tr(), N ); //[checked]

    // Flexural components
    auto E_f = ( deriv2(G,sn(G).normalized().tr()) - deriv2(defG,sn(defG).normalized().tr()) ) * reshape(m2,3,3) ; //[checked]
    auto S_f = E_f * reshape(mm,3,3);
    auto M   = tt.val() * tt.val() * tt.val() / 12.0 * S_f;

    auto E_f_der = -( deriv2(u,sn(defG).normalized().tr() ) + deriv2(defG,var1(u,defG) ) ) * reshape(m2,3,3); //[checked]
    auto S_f_der = E_f_der * reshape(mm,3,3);
    auto M_der   = tt.val() * tt.val() * tt.val() / 12.0 * S_f_der;

    auto E_f_der2 = - (flatdot2( deriv2(u), var1(u,defG).tr(), M  ).symmetrize() + var2(u,u,defG, M ));

    auto F        = ff;


    gsVector<> pt(2); pt.setConstant(0.25);
    gsVector<> pt2(3); pt2.setConstant(2);
    // gsMatrix<> pt(7,2);
    // pt<<0,0,
    // 0,0.5,
    // 0,1.0,
    // 0.5,0,
    // 1.0,0,
    // 0.5,0.5,
    // 1.0,1.0;
    // pt = pt.transpose();
    // gsDebugVar(pt);

    // ! [Solve linear problem]

    // assemble mass
    // A.assemble(u*u.tr());
    // gsDebugVar(A.matrix().toDense());
    // gsDebugVar(A.matrix().rows());
    // gsDebugVar(A.matrix().cols());


    // assemble system
    A.assemble(
        // (N_der * cartcon(G) * (E_m_der * cartcon(G)).tr() + M_der * cartcon(G) * (E_f_der * cartcon(G)).tr()) * meas(G)
        (N_der * (E_m_der).tr() + M_der * (E_f_der).tr()) * meas(G)
        ,
        // u * cartcon(G) * cartcon(G) * F  * meas(G)
        u * F  * meas(G) + pressure * u * sn(defG).normalized() * meas(G)
        );

    // evaluateFunction(ev, reshape(mmA,3,3), pt); // evaluates an expression on a point
    // evaluateFunction(ev, reshape(mmB,3,3), pt); // evaluates an expression on a point
    // evaluateFunction(ev, reshape(mmC,3,3), pt); // evaluates an expression on a point
    // evaluateFunction(ev, reshape(mmD,3,3), pt); // evaluates an expression on a point
    // evaluateFunction(ev, S0, pt); // evaluates an expression on a point
    // evaluateFunction(ev, S1, pt); // evaluates an expression on a point


    // evaluateFunction(ev, N_der * cartcon(G) * (E_m_der * cartcon(G)).tr(), pt); // evaluates an expression on a point
    // evaluateFunction(ev, (N_der * cartcon(G) * (E_m_der * cartcon(G)).tr()).tr(), pt); // evaluates an expression on a point

    // gsDebugVar((jac(defG).tr() * jac(u) * jac(defG) * jac(u).tr()).isMatrix());
    // gsDebugVar((jac(defG).tr() * jac(u) * jac(defG) * jac(u).tr()).colSpan());
    // gsDebugVar((jac(defG).tr() * jac(u) * jac(defG) * jac(u).tr()).rowSpan());
    // gsDebugVar((jac(defG).tr() * jac(u)).colSpan());
    // gsDebugVar((jac(defG).tr() * jac(u)).rowSpan());
    // gsDebugVar(((jac(defG).tr() * jac(u)).tr()).colSpan());
    // gsDebugVar(((jac(defG).tr() * jac(u)).tr()).rowSpan());

    // For Neumann (same for Dirichlet/Nitsche) conditions
    variable g_N = A.getBdrFunction();
    A.assembleRhsBc(u * g_N, bc.container("Neumann") );

    // for weak dirichlet (DOES THIS HANDLE COMPONENTS?)
    real_t alpha_d = 1e3;
    A.assembleLhsRhsBc
    (
        alpha_d * u * u.tr()
        ,
        alpha_d * u * (defG - G - g_N).tr()
        ,
        bc.container("dirichlet weak")
    );

    // for weak clamped
    real_t alpha_r = 1e3;
    A.assembleLhsRhsBc
    (
        // alpha_r * ( sn(defG).tr()*sn(G) - 1.0 ) * ( flatdot2??? )
        // +
        alpha_r * ( var1(u,defG) * sn(G).tr() ).symmetrize()
        ,
        alpha_r * ( sn(defG)*sn(G).tr() - sn(G)*sn(G).tr() ) * ( var1(u,defG) * sn(G).tr() )
        ,
        bc.container("clamped weak")
    );


    // gsDebugVar(A.matrix().toDense());
    // gsDebugVar(A.matrix().rows());
    // gsDebugVar(A.matrix().cols());
    // gsDebugVar(A.rhs().transpose());



    // solve system
    solver.compute( A.matrix() );
    gsMatrix<> solVector = solver.solve(A.rhs());

    // update deformed patch
    gsMatrix<> cc;

    u_sol.setSolutionVector(solVector);
    for ( size_t k =0; k!=mp_def.nPatches(); ++k) // Deform the geometry
    {
        // // extract deformed geometry
        u_sol.extract(cc, k);
        mp_def.patch(k).coefs() += cc;  // defG points to mp_def, therefore updated
    }

    // gsDebugVar(mp_def.patch(0).coefs().transpose());

    gsMatrix<> result;
    u_sol.extractFull(result);

    /*Something with Dirichlet homogenization*/

    // auto sol1 = G + u_sol;
    // auto sol2 = defG;

    // auto nsol1= sn(G);
    // // defG.registerData(G.source(), G.data())
    // auto nsol2= sn(defG);

    // evaluateFunction(ev, sol1, pt); // evaluates an expression on a point
    // evaluateFunction(ev, sol2, pt); // evaluates an expression on a point
    // evaluateFunction(ev, nsol1, pt); // evaluates an expression on a point
    // evaluateFunction(ev, nsol2, pt); // evaluates an expression on a point

    // evaluateFunction(ev, pressure * u * var1(u,defG) .tr(), pt); // evaluates an expression on a point
    // evaluateFunction(ev, N_der * E_m_der.tr(), pt); // evaluates an expression on a point

    gsVector<> pt3(2);
    pt3.setConstant(0.5);
    gsDebug<<ev.integral(meas(G))<<"\n";
    gsDebug<<ev.integral(meas(defG))<<"\n";
    gsDebug<<ev.eval(G,pt3)<<"\n";
    gsDebug<<ev.eval(defG,pt3)<<"\n";



    // ! [Solve linear problem]

    // ! [Solve nonlinear problem]
    real_t residual = A.rhs().norm();
    real_t residual0 = residual;
    real_t residualOld = residual;
    gsVector<> updateVector = solVector;
    if (nonlinear)
    {
        index_t itMax = 10;
        real_t tol = 1e-8;
        for (index_t it = 0; it != itMax; ++it)
        {

            A.initSystem(false);
            // assemble system
            A.assemble(
                (
                  N_der * E_m_der.tr()
                    +
                  E_m_der2
                    +
                  M_der * E_f_der.tr()
                    +
                  E_f_der2
                  ) * meas(G)
                    -
                  pressure * u * var1(u,defG) .tr() * meas(G)
                , u * F * meas(G) + pressure * u * sn(defG).normalized() * meas(G) - ( ( N * E_m_der.tr() + M * E_f_der.tr() ) * meas(G) ).tr()
                );

            // For Neumann (same for Dirichlet/Nitche) conditions
            variable g_N = A.getBdrFunction();
            A.assembleRhsBc(u * g_N, bc.neumannSides() );

            // A.assemble(tt.val() * tt.val() * tt.val() / 3.0 * E_f_der2);
            // solve system
            solver.compute( A.matrix() );
            gsMatrix<> updateVector = solver.solve(A.rhs()); // this is the UPDATE

            // gsDebugVar(A.matrix().toDense());
            // gsDebugVar(A.rhs().transpose());

            solVector += updateVector; // WHY??
            residual = A.rhs().norm();

            gsInfo<<"Iteration: "<< it
                   <<", residue: "<< residual
                   <<", update norm: "<<updateVector.norm()
                   <<", log(Ri/R0): "<< math::log10(residualOld/residual0)
                   <<", log(Ri+1/R0): "<< math::log10(residual/residual0)
                   <<"\n";

            residualOld = residual;

            // update deformed patch
            u_sol.setSolutionVector(updateVector); // WHY?>
            for ( size_t k =0; k!=mp_def.nPatches(); ++k) // Deform the geometry
            {
                // // extract deformed geometry
                u_sol.extract(cc, k);
                mp_def.patch(k).coefs() += cc;  // defG points to mp_def, therefore updated
            }

            if (residual < tol)
                break;
        }
    }

    // evaluateFunction(ev, defG, pt); // evaluates an expression on a point
    // evaluateFunction(ev, cartcov(defG).inv()*F, pt); // evaluates an expression on a point

    // ! [Solve nonlinear problem]

    // Penalize the matrix? (we need values for the DoFs to be enforced..
    // function/call:  penalize_matrix(DoF_indices, DoF_values)
    // otherwise: should we tag the DoFs inside "u" ?

    // gsInfo<<"RHS rows = "<<A.rhs().rows()<<"\n";
    // gsInfo<<"RHS cols = "<<A.rhs().cols()<<"\n";
    // gsInfo<<"MAT rows = "<<A.matrix().rows()<<"\n";
    // gsInfo<<"MAT cols = "<<A.matrix().cols()<<"\n";

    // gsInfo<< A.rhs().transpose() <<"\n";
    // gsInfo<< A.matrix().toDense()<<"\n";

    u_sol.setSolutionVector(solVector);
    mp_def = mp;
    for ( size_t k =0; k!=mp.nPatches(); ++k) // Deform the geometry
    {
        // // extract deformed geometry
        u_sol.extract(cc, k);
        mp_def.patch(k).coefs() += cc;  // defG points to mp_def, therefore updated
    }

    gsMultiPatch<> deformation = mp_def;
    for (index_t k = 0; k != mp_def.nPatches(); ++k)
        deformation.patch(k).coefs() -= mp.patch(k).coefs();

    //gsInfo <<"Deformation norm       : "<< deformation.patch(0).coefs().norm() <<".\n";
    gsInfo <<"Maximum deformation coef: "
           << deformation.patch(0).coefs().colwise().maxCoeff() <<".\n";
    gsInfo <<"Minimum deformation coef: "
           << deformation.patch(0).coefs().colwise().minCoeff() <<".\n";
    gsInfo <<"Area (undeformed) = "<<ev.integral(meas(G))<<"\tArea (undeformed) = "<<ev.integral(meas(defG))<<"\n";


    gsMatrix<> coords(2,1);
    if (testCase==1)
      coords<<0,0;
    else if (testCase==2)
      coords<<0.5,0;
    else if (testCase==3)
      coords<<0,0;
    else if (testCase==4)
      coords<<1,1;
    else
      coords<<0,0;

    gsMatrix<> res(3,1);
    mp.patch(0).eval_into(coords,res);
    real_t x=res.at(0);
    real_t y=res.at(1);
    real_t z=res.at(2);

    deformation.patch(0).eval_into(coords,res);
    real_t ux=res.at(0);
    real_t uy=res.at(1);
    real_t uz=res.at(2);

    gsInfo<<"Deformation on point ("<<x<<","<<y<<","<<z<<"):\n";
    gsInfo<<std::setprecision(20)<<"("<<ux<<","<<uy<<","<<uz<<")"<<"\n";

    //! [Export visualization in ParaView]
    if (plot)
    {
        gsField<> solField(mp, deformation);
        gsInfo<<"Plotting in Paraview...\n";
        gsWriteParaview<>( solField, "solution", 1000, true);
        // gsFileManager::open("solution.pvd");
    }

    return EXIT_SUCCESS;

}// end main

template <class T>
void evaluateFunction(gsExprEvaluator<T> ev, auto expression, gsVector<T> pt)
{
    gsMatrix<T> evresult = ev.eval( expression,pt );
    gsInfo << "Eval on point ("<<pt.at(0)<<" , "<<pt.at(1)<<") :\n"<< evresult;
    gsInfo << "\nEnd ("<< evresult.rows()<< " x "<<evresult.cols()<<")\n";
};

template <class T>
void evaluateFunction(gsExprEvaluator<T> ev, auto expression, gsMatrix<T> pt)
{
    gsVector<T> tmp;
    gsMatrix<T> evresult;
    for (index_t k=0; k!= pt.cols(); ++k)
    {
        tmp = pt.col(k);
        evresult = ev.eval( expression,tmp );
        gsInfo<<evresult<<"\n";
    }
};

template <class T>
gsMultiPatch<T> RectangularDomain(int n, int p, T L, T B)
{
  int q = p;
  int m = n;
  gsMultiPatch<T> mp = RectangularDomain(n, m, p, q, L, B);
  return mp;
}

template <class T>
gsMultiPatch<T> RectangularDomain(int n, int m, int p, int q, T L, T B)
{
  // -------------------------------------------------------------------------
  // --------------------------Make beam geometry-----------------------------
  // -------------------------------------------------------------------------
  int dim = 3; //physical dimension
  gsKnotVector<> kv0;
  kv0.initUniform(0,1,0,p+1,1);
  gsKnotVector<> kv1;
  kv1.initUniform(0,1,0,q+1,1);

  for(index_t i = 0; i< n; ++i)
      kv0.uniformRefine();
  for(index_t i = 0; i< m; ++i)
      kv1.uniformRefine();

  // Make basis
  gsTensorBSplineBasis<2,T> basis(kv0,kv1);

  // Initiate coefficient matrix
  gsMatrix<> coefs(basis.size(),dim);
  // Number of control points needed per component
  size_t len0 = basis.component(0).size();
  size_t len1 = basis.component(1).size();
  gsVector<> coefvec0(len0);
  // Uniformly distribute control points per component
  coefvec0.setLinSpaced(len0,0.0,L);
  gsVector<> coefvec1(basis.component(1).size());
  coefvec1.setLinSpaced(len1,0.0,B);

  // Z coordinate is zero
  coefs.col(2).setZero();

  // Define a matrix with ones
  gsVector<> temp(len0);
  temp.setOnes();
  for (index_t k = 0; k < len1; k++)
  {
    // First column contains x-coordinates (length)
    coefs.col(0).segment(k*len0,len0) = coefvec0;
    // Second column contains y-coordinates (width)
    coefs.col(1).segment(k*len0,len0) = temp*coefvec1.at(k);
  }
  // Create gsGeometry-derived object for the patch
  gsTensorBSpline<2,real_t> shape(basis,coefs);

  gsMultiPatch<T> mp;
  mp.addPatch(shape);
  mp.addAutoBoundaries();

  return mp;
}


/*
template<class T>
void gsShellAssembler<T>::applyLoads()
{
    gsMatrix<T>        bVals;
    gsMatrix<unsigned> acts,globalActs;

    for (size_t i = 0; i< m_pLoads.numLoads(); ++i )
    {
        if ( m_pLoads[i].parametric )
        {
            m_bases.front().basis(m_pLoads[i].patch).active_into( m_pLoads[i].point, acts );
            m_bases.front().basis(m_pLoads[i].patch).eval_into  ( m_pLoads[i].point, bVals);
        }
        else
        {
            gsMatrix<> forcePoint;
            m_patches.patch(m_pLoads[i].patch).invertPoints(m_pLoads[i].point,forcePoint);
            u.source().piece(m_pLoads[i].patch).active_into( forcePoint, acts );
            u.source().piece(m_pLoads[i].patch).active_into( forcePoint, bVals);
        }

        // translate patch-local indices to global dof indices
        for (size_t j = 0; j< 3; ++j)
        {
            if (m_pLoads[i].value[j] != 0.0)
            {
                u.dofMappers[j].localToGlobal(acts, m_pLoads[i].patch, globalActs);

                for (index_t k=0; k < globalActs.rows(); ++k)
                {
                    if (int(globalActs(k,0)) < m_dofs)
                        m_rhs(globalActs(k,0), 0) += bVals(k,0) * m_pLoads[i].value[j];
                }
            }
        }
    }
}
*/
