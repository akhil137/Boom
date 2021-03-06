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

#include <Models/SpdData.hpp>
#include <LinAlg/Cholesky.hpp>
#include <cpputil/report_error.hpp>

namespace BOOM {

  namespace {
    typedef std::shared_ptr<SPD::SpdStorage>  SpdPtr;
    typedef std::shared_ptr<SPD::CholStorage> CholPtr;
    typedef std::shared_ptr<SPD::Storage> StoragePtr;

    // Given four equivalent representations of the data in an SpdData
    // object, find the current representation, and use it to refresh
    // the primary representation.
    inline void ensure_current(
        SPD::SpdStorage *sig,
        SPD::CholStorage *sig_chol,
        SPD::SpdStorage *siginv,
        SPD::CholStorage *siginv_chol) {
      if (sig->current()) {
        return;
      } else if (sig_chol->current()) {
        sig->refresh_from_chol(*sig_chol);
      } else if (siginv_chol->current()) {
        sig->refresh_from_chol(*siginv_chol, true);
      } else if (siginv->current()) {
        siginv_chol->refresh(*siginv);
        sig->refresh_from_chol(*siginv_chol, true);
      } else {
        report_error("I'm lost in SpdData::ensure_current");
      }
    }

    // Given four equivalent representations of the data in an SpdData
    // object, find the current representation, and use it to refresh
    // the primary representation.
    inline void ensure_chol_current(SPD::CholStorage *chol,
                                    SPD::SpdStorage *sig,
                                    SPD::CholStorage *siginv_chol,
                                    SPD::SpdStorage *siginv) {
      if (chol->current()) {
        return;
      } else if (sig->current()) {
        chol->refresh(*sig);
      } else if (siginv_chol->current()) {
        sig->refresh_from_chol(*siginv_chol, true);
      } else if (siginv->current()) {
        siginv_chol->refresh(*siginv);
      } else {
        std::ostringstream err;
        err << "I'm lost in SpdData::ensure_chol_current.  "
            << "None of the representations are current."
            << endl;
        report_error(err.str());
      }
    }
  }

  namespace SPD {

    Storage::Storage(bool cur)
      : current_(cur)
    {}

    Storage::Storage(const Storage &rhs)
      : current_(rhs.current_)
    {
      // signals are not copied
    }

    Storage::~Storage() {}

    uint Storage::size(bool min) const {
      uint d = dim();
      return min ? d*(d+1)/2 : d*d;
    }

    bool Storage::current() const {
      return current_;
    }

    void Storage::set_current() {
      current_ = true;
    }

    void Storage::signal() {
      uint n = signals_.size();
      for (uint i = 0; i < n; ++i) signals_[i]();
    }

    std::function<void(void)> Storage::create_observer() {
      return [this]() {this->observe_changes();};
    }

    void Storage::observe_changes() {current_ = false;}

    void Storage::add_observer(std::function<void(void)> f) {
      signals_.push_back(f);}

  //______________________________________________________________________

    CholStorage::CholStorage()
      : Storage(false)
    {}

    CholStorage::CholStorage(const SpdMatrix &S)
      : Storage(true),
        L(Chol(S).getL())
    {}

    CholStorage::CholStorage(const CholStorage &rhs)
      : Storage(rhs),
        L(rhs.L)
    {}

    CholStorage * CholStorage::clone() const {
      return new CholStorage(*this);}


    uint CholStorage::dim() const { return L.nrow(); }

    void CholStorage::set(const Matrix &arg, bool sig) {
      L = arg;
      set_current();
      if (sig) signal();
    }

    const Matrix & CholStorage::value() const {
      return L;
    }

    void CholStorage::refresh(const SpdStorage &S) {
      Chol chol(S.value());
      L = chol.getL();
      set_current();
    }

    //_____________________________________________________________________

    SpdStorage::SpdStorage()
      : Storage(false)
    {}

    SpdStorage::SpdStorage(const SpdMatrix &S)
      : Storage(true),
        sig_(S)
    {}

    SpdStorage::SpdStorage(const SpdStorage & rhs)
      : Storage(rhs),
        sig_(rhs.sig_)
    {}

    SpdStorage * SpdStorage::clone() const {
      return new SpdStorage(*this);}


    const SpdMatrix & SpdStorage::value() const { return sig_; }

    uint SpdStorage::dim() const { return sig_.nrow();}

    void SpdStorage::set(const SpdMatrix &S, bool sig) {
      sig_ = S;
      set_current();
      if (sig) signal();
    }

    void SpdStorage::refresh_from_chol(const CholStorage &chol, bool inv) {
      const Matrix &L(chol.value());
      if (inv) {
        sig_ = chol2inv(L);
      } else {
        sig_ = LLT(L);
      }
      set_current();
    }

    void SpdStorage::refresh_from_inv(const SpdStorage &S, CholStorage &chol) {
      chol.refresh(S);
      refresh_from_chol(chol, true);
    }
  }  // namespace SPD

  //______________________________________________________________________
  using namespace SPD;

  SpdData::SpdData(uint n, double diag, bool ivar)
    : var_(ivar ? new SpdStorage : new SpdStorage(SpdMatrix(n, diag))),
      ivar_(ivar ? new SpdStorage(SpdMatrix(n, diag)) : new SpdStorage),
      var_chol_(new CholStorage),
      ivar_chol_(new CholStorage)
  {
    setup_storage();
    current_rep_ = ivar ? ivar_ : var_;
  }

  SpdData::SpdData(const SpdMatrix & S, bool ivar)
    : var_(ivar ? new SpdStorage : new SpdStorage(S)),
      ivar_(ivar? new SpdStorage(S) : new SpdStorage),
      var_chol_(new CholStorage),
      ivar_chol_(new CholStorage)
  {
    setup_storage();
    current_rep_ = ivar ? ivar_ : var_;
  }

  SpdData::SpdData(const SpdData & rhs)
    : Data(rhs),
      Traits(rhs),
      var_(rhs.var_->clone()),
      ivar_(rhs.ivar_->clone()),
      var_chol_(rhs.var_chol_->clone()),
      ivar_chol_(rhs.ivar_chol_->clone())
  {
    setup_storage();
    if (rhs.current_rep_ == rhs.var_) current_rep_ = var_;
    else if (rhs.current_rep_ == rhs.ivar_) current_rep_ = ivar_;
    else if (rhs.current_rep_ == rhs.var_chol_) current_rep_ = var_chol_;
    else if (rhs.current_rep_ == rhs.ivar_chol_) current_rep_ = ivar_chol_;
  }

  SpdData * SpdData::clone() const {return new SpdData(*this);}

  uint SpdData::size(bool min) const {
    return current_rep_->size(min); }

  uint SpdData::dim() const { return current_rep_->dim();}

  void SpdData::setup_storage() {
    std::vector<StoragePtr> storage;

    storage.push_back(var_);
    storage.push_back(ivar_);
    storage.push_back(ivar_chol_);
    storage.push_back(var_chol_);

    for (uint i = 0; i < storage.size(); ++i) {
      StoragePtr obs = storage[i];
      for (uint j = 0; j < storage.size(); ++j) {
        if (j != i) {
          obs->add_observer(storage[j]->create_observer());
        }
      }
    }
  }

  ostream & SpdData::display(ostream &out) const {
    out << var() << endl;
    return out;
  }

  const SpdMatrix & SpdData::value() const { return var();}

  void SpdData::set(const SpdMatrix & V, bool sig) {
    set_var(V, sig);
  }

  const SpdMatrix & SpdData::var() const {
    if (!var_->current()) {
      ensure_var_current();
    }
    return var_->value();
  }

  const SpdMatrix & SpdData::ivar() const {
    if (!ivar_->current()) {
      ensure_ivar_current();
    }
    return ivar_->value();
  }

  void SpdData::ensure_ivar_current() const {
    ensure_current(ivar_.get(),
                   ivar_chol_.get(),
                   var_.get(),
                   var_chol_.get());}

  void SpdData::ensure_var_current() const {
    ensure_current(var_.get(),
                   var_chol_.get(),
                   ivar_.get(),
                   ivar_chol_.get());
  }

  void SpdData::ensure_ivar_chol_current() const {
    ensure_chol_current(ivar_chol_.get(),
                        ivar_.get(),
                        var_chol_.get(),
                        var_.get());
  }

  void SpdData::ensure_var_chol_current() const {
    ensure_chol_current(var_chol_.get(),
                        var_.get(),
                        ivar_chol_.get(),
                        ivar_.get());
  }

  const Matrix & SpdData::ivar_chol() const {
    if (!ivar_chol_->current()) {
      ensure_ivar_chol_current();
    }
    return ivar_chol_->value();
  }

  const Matrix & SpdData::var_chol() const {
    if (!var_chol_->current()) {
      ensure_var_chol_current();
    }
    return var_chol_->value();
  }

  double SpdData::ldsi() const {
    bool inv = (ivar_->current() || ivar_chol_->current());
    const Matrix & L(inv ? ivar_chol() : var_chol());
    ConstVectorView v(L.diag());
    double ans = 0;
    uint n = v.size();
    for (uint i = 0; i < n; ++i) ans += log(fabs(v[i]));
    ans *= 2;
    return inv ? ans : -ans;
  }

  void SpdData::set_var(const SpdMatrix & var, bool sig) {
    var_->set(var, sig);
    current_rep_ = var_;
  }
  void SpdData::set_ivar(const SpdMatrix & ivar, bool sig) {
    ivar_->set(ivar, sig);
    current_rep_ = ivar_;
  }
  void SpdData::set_ivar_chol(const Matrix & L, bool sig) {
    ivar_chol_->set(L, sig);
    current_rep_ = ivar_chol_;
  }
  void SpdData::set_var_chol(const Matrix & L, bool sig) {
    var_chol_->set(L, sig);
    current_rep_ = var_chol_;
  }

  void SpdData::set_S_Rchol(const Vector &sd, const Matrix &L) {
    Matrix C(L);
    uint n = C.nrow();
    assert(sd.size() == n);
    for (uint i = 0; i < n; ++i) C.row(i) *= sd[i];
    set_var_chol(C);
  }

}  // namespace BOOM
