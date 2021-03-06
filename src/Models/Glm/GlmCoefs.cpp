/*
  Copyright (C) 2007 Steven L. Scott

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/

#include <Models/Glm/GlmCoefs.hpp>
#include <stdexcept>
#include <cpputil/report_error.hpp>

namespace BOOM{

  GlmCoefs::GlmCoefs(uint p, bool all) // 0..p-1
    : VectorParams(p),
      inc_(p, all),
      included_coefficients_current_(false)
  {
    if(!all) add(0); // start with intercept
  }

  GlmCoefs::GlmCoefs(const Vector &b, bool infer_model_selection)
    : VectorParams(b),
      inc_(b.size()),
      included_coefficients_current_(false)
  {
    if(infer_model_selection) inc_from_beta(b);
  }

  GlmCoefs::GlmCoefs(const Vector &b, const Selector &Inc)
    : VectorParams(b),
      inc_(Inc),
      included_coefficients_current_(false)
  {
    //    assert(Inc.nvars_possible()==b.size());
    uint n = inc_.nvars();
    uint N = inc_.nvars_possible();

    if(n>N){
      ostringstream err;
      err << "Something has gone horribly wrong building "
          << "GlmCoefs.  nvars_possible = " << N
          << " but nvars = " << n << ".  explain that one."
          << endl;
      report_error(err.str());
    }
    uint p = b.size();
    if(p>N){
      ostringstream err;
      err << "cannot build GlmCoefs with vector of size "
          << p << " and 'Selector' of size "
          << N << ". " << endl;
      report_error(err.str());
    }

    if(p<N){
      if(p==n){
        VectorParams::set(Inc.expand(b), false);
      }else{
        ostringstream err;
        err << "size of 'b' passed to constructor for GlmCoefs "
            << " (" << p << ") must match either nvars ("
            << n << ") or nvars_possible (" << N
            << ")." << endl;
        report_error(err.str());
      }
    }
  }

  GlmCoefs::GlmCoefs(const GlmCoefs &rhs)
    : Data(rhs),
      Params(rhs),
      VectorParams(rhs),
      inc_(rhs.inc_),
      included_coefficients_current_(false)
  {}

  GlmCoefs * GlmCoefs::clone()const{ return new GlmCoefs(*this); }

  //-------------- model selection -------------

  const Selector & GlmCoefs::inc()const{ return inc_;}

  bool GlmCoefs::inc(uint p)const{ return inc_[p];}

  void GlmCoefs::set_inc(const Selector &new_inc){
    assert(new_inc.nvars_possible() == inc_.nvars_possible());
    uint n = nvars();
    for(uint i=0; i<n; ++i){
      uint I = indx(i);
      if(!new_inc[I]) Beta(I)=0;
    }
    inc_ = new_inc;
  }

  void GlmCoefs::add(uint i){
    included_coefficients_current_ = false;
    inc_.add(i); }

  void GlmCoefs::drop(uint i){
    inc_.drop(i);
    Beta(i)=0;
  }

  void GlmCoefs::flip(uint i){
    if(inc_[i]) drop(i);
    else add(i);
  }

  void GlmCoefs::drop_all(){
    inc_.drop_all();
    set_Beta(Vector(nvars_possible()));
  }

  void GlmCoefs::add_all(){
    inc_.add_all();
  }

  //------------------- size querries ----------------

  uint GlmCoefs::nvars()const{return inc().nvars();}
  uint GlmCoefs::nvars_possible()const{ return inc().nvars_possible(); }
  uint GlmCoefs::nvars_excluded()const{ return inc().nvars_excluded(); }
  uint GlmCoefs::size(bool minimal)const{
    return minimal ? nvars() : nvars_possible();}

  //-------------------- prediction ------------

  namespace{
    template<class VEC>
    double do_prediction(const GlmCoefs *beta, const VEC &x){
      uint nb = beta->nvars();
      if(nb == 0) return 0;
      uint nx = x.size();
      uint Nb = beta->nvars_possible();
      if(nx == Nb) {
        return x.dot(beta->Beta());
      } else if(nx == nb) {
        return x.dot(beta->included_coefficients());
      } else{
        ostringstream msg;
        msg << "incompatible covariates in GlmCoefs::predict" << endl
            << "beta = " << beta->Beta() << endl
            << "x = " << x << endl;
        report_error(msg.str());
      }
      return 0;
    }
  }

  double GlmCoefs::predict(const Vector &x)const{
    return do_prediction(this, x); }

  double GlmCoefs::predict(const VectorView &x)const{
    return do_prediction(this,x); }

  double GlmCoefs::predict(const ConstVectorView &x)const{
    return do_prediction(this, x); }

  Vector GlmCoefs::predict(const Matrix &design_matrix)const{
    Vector ans(design_matrix.nrow());
    predict(design_matrix, VectorView(ans));
    return ans;
  }

  void GlmCoefs::predict(const Matrix &design_matrix, Vector &ans)const{
    predict(design_matrix, VectorView(ans));
  }

  void GlmCoefs::predict(const Matrix &design_matrix, VectorView ans)const{
    uint number_of_variables_included = this->nvars();
    uint total_number_of_variables = this->nvars_possible();
    if (number_of_variables_included >= .25 * total_number_of_variables) {
      ans = design_matrix * Beta();
    } else {
      ans = 0;
      inc().sparse_multiply(design_matrix, Beta(), ans);
    }
  }

  //------ operations for included variables -----

  Vector GlmCoefs::included_coefficients()const{
    if(!included_coefficients_current_) fill_beta();
    return included_coefficients_;}

  void GlmCoefs::set_included_coefficients(const Vector &b){
    if(b.size()!=nvars()) wrong_size_beta(b);
    set_Beta(inc_.expand(b));
  }

  void GlmCoefs::set_included_coefficients(
      const Vector &b, const Selector &inc) {
    if (b.size() != inc.nvars()) wrong_size_beta(b);
    set_Beta(inc.expand(b));
    included_coefficients_ = b;
    included_coefficients_current_ = true;
    inc_ = inc;
  }

  //------- operations on all possible variables ------

  const Vector & GlmCoefs::Beta()const{
    return VectorParams::value();   }

  double GlmCoefs::Beta(uint i)const{
    return VectorParams::value()[i]; }

  double & GlmCoefs::Beta(uint i){
    included_coefficients_current_ = false;
    return VectorParams::operator[](i);}

  void GlmCoefs::set_Beta(const Vector &tmp){
    if (tmp.size() != nvars_possible()) {
      std::ostringstream err;
      err << "set_Beta called with wrong size input." << std::endl
          << "current size = " << nvars_possible() << endl
          << "Beta.size()  = " << tmp.size() << endl;
      report_error(err.str());
    }
    included_coefficients_current_ = false;
    VectorParams::set(tmp);
  }

  // Drop coefficients whose value is zero.  Add coefficients whose
  // value is nonzero.
  void GlmCoefs::infer_sparsity() {
    const Vector &beta(Beta());
    for (int i = 0; i < beta.size(); ++i) {
      if (beta[i] == 0.0) {
        drop(i);
      } else {
        add(i);
      }
    }
  }

  //------- virtual function overloads ---------------

  Vector GlmCoefs::vectorize(bool minimal)const{
    if(minimal) return included_coefficients();
    return VectorParams::vectorize();
  }

  Vector::const_iterator GlmCoefs::unvectorize(
      Vector::const_iterator &v, bool minimal){
    included_coefficients_current_ = false;
    if(minimal){
      included_coefficients_.resize(nvars());
      Vector::const_iterator e = v+included_coefficients_.size();
      std::copy(v,e, included_coefficients_.begin());
      set_included_coefficients(included_coefficients_);
      return e;
    }
    return VectorParams::unvectorize(v);
  }

  Vector::const_iterator GlmCoefs::unvectorize(const Vector &v, bool min){
    Vector::const_iterator b = v.begin();
    return unvectorize(b, min);}

  //____________________ private stuff ___________

  void GlmCoefs::inc_from_beta(const Vector &b){
    uint n = b.size();
    for(uint i =0; i<n; ++i){
      if(b[i]!=0) add(i);
      else drop(i);}
  }

  void GlmCoefs::wrong_size_beta(const Vector &b)const{
    ostringstream msg;
    msg << "wrong size argument given to set_beta" << endl
        << "current size  = " << nvars() << endl
        << "argument size = " << b.size() << endl;
    report_error(msg.str());
  }

  void GlmCoefs::fill_beta()const{
    included_coefficients_ = inc_.select(Beta());
    included_coefficients_current_ = true;
  }

}  // namespace BOOM
