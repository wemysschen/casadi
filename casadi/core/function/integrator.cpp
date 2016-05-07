/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2014 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            K.U. Leuven. All rights reserved.
 *    Copyright (C) 2011-2014 Greg Horn
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */


#include "integrator_impl.hpp"
#include "../std_vector_tools.hpp"

using namespace std;
namespace casadi {

  bool has_integrator(const string& name) {
    return Integrator::hasPlugin(name);
  }

  void load_integrator(const string& name) {
    Integrator::loadPlugin(name);
  }

  string doc_integrator(const string& name) {
    return Integrator::getPlugin(name).doc;
  }

  Function integrator(const string& name, const string& solver,
                      const SXDict& dae, const Dict& opts) {
    return integrator(name, solver, Integrator::map2problem(dae), opts);
  }

  Function integrator(const string& name, const string& solver,
                      const MXDict& dae, const Dict& opts) {
    return integrator(name, solver, Integrator::map2problem(dae), opts);
  }

  Function integrator(const string& name, const string& solver,
                      const Function& dae, const Dict& opts) {
    Function ret;
    ret.assignNode(Integrator::getPlugin(solver).creator(name, dae));
    ret->construct(opts);
    return ret;
  }

  vector<string> integrator_in() {
    vector<string> ret(integrator_n_in());
    for (size_t i=0; i<ret.size(); ++i) ret[i]=integrator_in(i);
    return ret;
  }

  vector<string> integrator_out() {
    vector<string> ret(integrator_n_out());
    for (size_t i=0; i<ret.size(); ++i) ret[i]=integrator_out(i);
    return ret;
  }

  string integrator_in(int ind) {
    switch (static_cast<IntegratorInput>(ind)) {
    case INTEGRATOR_X0:  return "x0";
    case INTEGRATOR_P:   return "p";
    case INTEGRATOR_Z0:  return "z0";
    case INTEGRATOR_RX0: return "rx0";
    case INTEGRATOR_RP:  return "rp";
    case INTEGRATOR_RZ0: return "rz0";
    case INTEGRATOR_NUM_IN: break;
    }
    return string();
  }

  string integrator_out(int ind) {
    switch (static_cast<IntegratorOutput>(ind)) {
    case INTEGRATOR_XF:  return "xf";
    case INTEGRATOR_QF:  return "qf";
    case INTEGRATOR_ZF:  return "zf";
    case INTEGRATOR_RXF: return "rxf";
    case INTEGRATOR_RQF: return "rqf";
    case INTEGRATOR_RZF: return "rzf";
    case INTEGRATOR_NUM_OUT: break;
    }
    return string();
  }

  int integrator_n_in() {
    return INTEGRATOR_NUM_IN;
  }

  int integrator_n_out() {
    return INTEGRATOR_NUM_OUT;
  }

  Integrator::Integrator(const std::string& name, const Function& oracle)
    : OracleFunction(name, oracle) {

    // Negative number of parameters for consistancy checking
    np_ = -1;

    // Get the sparsities
    t_ = oracle_.sparsity_in(DE_T);
    x_ = oracle_.sparsity_in(DE_X);
    z_ = oracle_.sparsity_in(DE_Z);
    p_ = oracle_.sparsity_in(DE_P);
    q_ = oracle_.sparsity_out(DE_QUAD);
    rx_ = oracle_.sparsity_in(DE_RX);
    rz_ = oracle_.sparsity_in(DE_RZ);
    rp_ = oracle_.sparsity_in(DE_RP);
    rq_ = oracle_.sparsity_out(DE_RQUAD);

    // Default options
    print_stats_ = false;
    output_t0_ = false;
  }

  Integrator::~Integrator() {
  }

  Sparsity Integrator::get_sparsity_in(int i) {
    switch (static_cast<IntegratorInput>(i)) {
    case INTEGRATOR_X0: return x_;
    case INTEGRATOR_P: return p_;
    case INTEGRATOR_Z0: return z_;
    case INTEGRATOR_RX0: return repmat(rx_, 1, ntout_);
    case INTEGRATOR_RP: return repmat(rp_, 1, ntout_);
    case INTEGRATOR_RZ0: return repmat(rz_, 1, ntout_);
    case INTEGRATOR_NUM_IN: break;
    }
    return Sparsity();
  }

  Sparsity Integrator::get_sparsity_out(int i) {
    switch (static_cast<IntegratorOutput>(i)) {
    case INTEGRATOR_XF: return repmat(x_, 1, ntout_);
    case INTEGRATOR_QF: return repmat(q_, 1, ntout_);
    case INTEGRATOR_ZF: return repmat(z_, 1, ntout_);
    case INTEGRATOR_RXF: return rx_;
    case INTEGRATOR_RQF: return rq_;
    case INTEGRATOR_RZF: return rz_;
    case INTEGRATOR_NUM_OUT: break;
    }
    return Sparsity();
  }

  void Integrator::
  eval(void* mem, const double** arg, double** res, int* iw, double* w) const {
    auto m = static_cast<IntegratorMemory*>(mem);

    // Read inputs
    const double* x0 = arg[INTEGRATOR_X0];
    const double* z0 = arg[INTEGRATOR_Z0];
    const double* p = arg[INTEGRATOR_P];
    const double* rx0 = arg[INTEGRATOR_RX0];
    const double* rz0 = arg[INTEGRATOR_RZ0];
    const double* rp = arg[INTEGRATOR_RP];
    arg += INTEGRATOR_NUM_IN;

    // Read outputs
    double* x = res[INTEGRATOR_XF];
    double* z = res[INTEGRATOR_ZF];
    double* q = res[INTEGRATOR_QF];
    double* rx = res[INTEGRATOR_RXF];
    double* rz = res[INTEGRATOR_RZF];
    double* rq = res[INTEGRATOR_RQF];
    res += INTEGRATOR_NUM_OUT;

    // Setup memory object
    setup(m, arg, res, iw, w);

    // Reset solver, take time to t0
    reset(m, grid_.front(), x0, z0, p);

    // Integrate forward
    for (int k=0; k<grid_.size(); ++k) {
      // Skip t0?
      if (k==0 && !output_t0_) continue;

      // Integrate forward
      advance(m, grid_[k], x, z, q);
      if (x) x += x_.nnz();
      if (z) z += z_.nnz();
      if (q) q += q_.nnz();
    }

    // If backwards integration is needed
    if (nrx_>0) {
      // Integrate backward
      resetB(m, grid_.back(), rx0, rz0, rp);

      // Proceed to t0
      retreat(m, grid_.front(), rx, rz, rq);
    }

    // Print statistics
    if (print_stats_) printStats(m, userOut());
  }

  Options Integrator::options_
  = {{&FunctionInternal::options_},
     {{"expand",
       {OT_BOOL,
        "Replace MX with SX expressions in problem formulation [false]"}},
      {"print_stats",
       {OT_BOOL,
        "Print out statistics after integration"}},
      {"t0",
       {OT_DOUBLE,
        "Beginning of the time horizon"}},
      {"tf",
       {OT_DOUBLE,
        "End of the time horizon"}},
      {"grid",
       {OT_DOUBLEVECTOR,
        "Time grid"}},
      {"augmented_options",
       {OT_DICT,
        "Options to be passed down to the augmented integrator, if one is constructed."}},
      {"output_t0",
       {OT_BOOL,
        "Output the state at the initial time"}}
     }
  };

  void Integrator::init(const Dict& opts) {
    // Default (temporary) options
    double t0=0, tf=1;
    bool expand = false;

    // Read options
    for (auto&& op : opts) {
      if (op.first=="expand") {
        expand = op.second;
      } else if (op.first=="output_t0") {
        output_t0_ = op.second;
      } else if (op.first=="print_stats") {
        print_stats_ = op.second;
      } else if (op.first=="grid") {
        grid_ = op.second;
      } else if (op.first=="augmented_options") {
        augmented_options_ = op.second;
      } else if (op.first=="t0") {
        t0 = op.second;
      } else if (op.first=="tf") {
        tf = op.second;
      }
    }

    // Replace MX oracle with SX oracle?
    if (expand) this->expand();

    // Store a copy of the options, for creating augmented integrators
    opts_ = opts;

    // If grid unset, default to [t0, tf]
    if (grid_.empty()) {
      grid_ = {t0, tf};
    }

    ngrid_ = grid_.size();
    ntout_ = output_t0_ ? ngrid_ : ngrid_-1;

    // Call the base class method
    FunctionInternal::init(opts);

    // For sparsity pattern propagation
    alloc(oracle_);

    // Get dimensions
    nx_ = x().nnz();
    nz_ = z().nnz();
    nq_ = q().nnz();
    np_  = p().nnz();
    nrx_ = rx().nnz();
    nrz_ = rz().nnz();
    nrp_ = rp().nnz();
    nrq_ = rq().nnz();

    // Warn if sparse inputs (was previously an error)
    casadi_assert_warning(oracle_.sparsity_in(DE_X).is_dense(),
                          "Sparse states in integrators are experimental");

    // Get the sparsities and BTF factorization of the forward and reverse DAE
    sp_jac_dae_ = sp_jac_dae();
    btf_jac_dae_ = sp_jac_dae_.btf();
    if (nrx_>0) {
      sp_jac_rdae_ = sp_jac_rdae();
      btf_jac_rdae_ = sp_jac_rdae_.btf();
    }

    // Allocate sufficiently large work vectors
    alloc_w(nx_+nz_);
    alloc_w(nrx_+nrz_);
    alloc_w(nx_ + nz_ + nrx_ + nrz_, true);
  }

  void Integrator::init_memory(void* mem) const {
    OracleFunction::init_memory(mem);
  }

  template<typename MatType>
  map<string, MatType> Integrator::aug_fwd(int nfwd) {
    log("Integrator::aug_fwd", "call");

    // Get input expressions
    vector<MatType> arg = MatType::get_input(oracle_);
    vector<MatType> aug_x, aug_z, aug_p, aug_rx, aug_rz, aug_rp;
    MatType aug_t = arg.at(DE_T);
    aug_x.push_back(arg.at(DE_X));
    aug_z.push_back(arg.at(DE_Z));
    aug_p.push_back(arg.at(DE_P));
    aug_rx.push_back(arg.at(DE_RX));
    aug_rz.push_back(arg.at(DE_RZ));
    aug_rp.push_back(arg.at(DE_RP));

    // Get output expressions
    vector<MatType> res = oracle_(arg);
    vector<MatType> aug_ode, aug_alg, aug_quad, aug_rode, aug_ralg, aug_rquad;
    aug_ode.push_back(res.at(DE_ODE));
    aug_alg.push_back(res.at(DE_ALG));
    aug_quad.push_back(res.at(DE_QUAD));
    aug_rode.push_back(res.at(DE_RODE));
    aug_ralg.push_back(res.at(DE_RALG));
    aug_rquad.push_back(res.at(DE_RQUAD));

    // Zero of time dimension
    MatType zero_t = MatType::zeros(t());

    // Forward directional derivatives
    vector<vector<MatType>> seed(nfwd, vector<MatType>(DE_NUM_IN));
    for (int d=0; d<nfwd; ++d) {
      seed[d][DE_T] = zero_t;
      string pref = "aug" + to_string(d) + "_";
      aug_x.push_back(seed[d][DE_X] = MatType::sym(pref + "x", x()));
      aug_z.push_back(seed[d][DE_Z] = MatType::sym(pref + "z", z()));
      aug_p.push_back(seed[d][DE_P] = MatType::sym(pref + "p", p()));
      aug_rx.push_back(seed[d][DE_RX] = MatType::sym(pref + "rx", rx()));
      aug_rz.push_back(seed[d][DE_RZ] = MatType::sym(pref + "rz", rz()));
      aug_rp.push_back(seed[d][DE_RP] = MatType::sym(pref + "rp", rp()));
    }

    // Calculate directional derivatives
    vector<vector<MatType>> sens;
    oracle_.forward(arg, res, seed, sens, true);

    // Collect sensitivity equations
    casadi_assert(sens.size()==nfwd);
    for (int d=0; d<nfwd; ++d) {
      casadi_assert(sens[d].size()==DE_NUM_OUT);
      aug_ode.push_back(project(sens[d][DE_ODE], x()));
      aug_alg.push_back(project(sens[d][DE_ALG], z()));
      aug_quad.push_back(project(sens[d][DE_QUAD], q()));
      aug_rode.push_back(project(sens[d][DE_RODE], rx()));
      aug_ralg.push_back(project(sens[d][DE_RALG], rz()));
      aug_rquad.push_back(project(sens[d][DE_RQUAD], rq()));
    }

    // Construct return object
    map<string, MatType> ret;
    ret["t"] = aug_t;
    ret["x"] = horzcat(aug_x);
    ret["z"] = horzcat(aug_z);
    ret["p"] = horzcat(aug_p);
    ret["ode"] = horzcat(aug_ode);
    ret["alg"] = horzcat(aug_alg);
    ret["quad"] = horzcat(aug_quad);
    ret["rx"] = horzcat(aug_rx);
    ret["rz"] = horzcat(aug_rz);
    ret["rp"] = horzcat(aug_rp);
    ret["rode"] = horzcat(aug_rode);
    ret["ralg"] = horzcat(aug_ralg);
    ret["rquad"] = horzcat(aug_rquad);
    return ret;
  }

  template<typename MatType>
  map<string, MatType> Integrator::aug_adj(int nadj) {
    log("Integrator::aug_adj", "call");
    // Get input expressions
    vector<MatType> arg = MatType::get_input(oracle_);
    vector<MatType> aug_x, aug_z, aug_p, aug_rx, aug_rz, aug_rp;
    MatType aug_t = arg.at(DE_T);
    aug_x.push_back(arg.at(DE_X));
    aug_z.push_back(arg.at(DE_Z));
    aug_p.push_back(arg.at(DE_P));
    aug_rx.push_back(arg.at(DE_RX));
    aug_rz.push_back(arg.at(DE_RZ));
    aug_rp.push_back(arg.at(DE_RP));

    // Get output expressions
    vector<MatType> res = oracle_(arg);
    vector<MatType> aug_ode, aug_alg, aug_quad, aug_rode, aug_ralg, aug_rquad;
    aug_ode.push_back(res.at(DE_ODE));
    aug_alg.push_back(res.at(DE_ALG));
    aug_quad.push_back(res.at(DE_QUAD));
    aug_rode.push_back(res.at(DE_RODE));
    aug_ralg.push_back(res.at(DE_RALG));
    aug_rquad.push_back(res.at(DE_RQUAD));

    // Zero of time dimension
    MatType zero_t = MatType::zeros(t());

    // Reverse mode directional derivatives
    vector<vector<MatType>> seed(nadj, vector<MatType>(DE_NUM_OUT));
    for (int d=0; d<nadj; ++d) {
      string pref = "aug" + to_string(d) + "_";
      aug_rx.push_back(seed[d][DE_ODE] = MatType::sym(pref + "ode", x()));
      aug_rz.push_back(seed[d][DE_ALG] = MatType::sym(pref + "alg", z()));
      aug_rp.push_back(seed[d][DE_QUAD] = MatType::sym(pref + "quad", q()));
      aug_x.push_back(seed[d][DE_RODE] = MatType::sym(pref + "rode", rx()));
      aug_z.push_back(seed[d][DE_RALG] = MatType::sym(pref + "ralg", rz()));
      aug_p.push_back(seed[d][DE_RQUAD] = MatType::sym(pref + "rquad", rq()));
    }

    // Calculate directional derivatives
    vector<vector<MatType>> sens;
    oracle_.reverse(arg, res, seed, sens, true);

    // Collect sensitivity equations
    casadi_assert(sens.size()==nadj);
    for (int d=0; d<nadj; ++d) {
      casadi_assert(sens[d].size()==DE_NUM_IN);
      aug_rode.push_back(project(sens[d][DE_X], x()));
      aug_ralg.push_back(project(sens[d][DE_Z], z()));
      aug_rquad.push_back(project(sens[d][DE_P], p()));
      aug_ode.push_back(project(sens[d][DE_RX], rx()));
      aug_alg.push_back(project(sens[d][DE_RZ], rz()));
      aug_quad.push_back(project(sens[d][DE_RP], rp()));
    }

    // Construct return object
    map<string, MatType> ret;
    ret["t"] = aug_t;
    ret["x"] = horzcat(aug_x);
    ret["z"] = horzcat(aug_z);
    ret["p"] = horzcat(aug_p);
    ret["ode"] = horzcat(aug_ode);
    ret["alg"] = horzcat(aug_alg);
    ret["quad"] = horzcat(aug_quad);
    ret["rx"] = horzcat(aug_rx);
    ret["rz"] = horzcat(aug_rz);
    ret["rp"] = horzcat(aug_rp);
    ret["rode"] = horzcat(aug_rode);
    ret["ralg"] = horzcat(aug_ralg);
    ret["rquad"] = horzcat(aug_rquad);
    return ret;
  }

  void Integrator::sp_fwd(const bvec_t** arg, bvec_t** res, int* iw, bvec_t* w, int mem) {
    log("Integrator::sp_fwd", "begin");

    // Work vectors
    bvec_t *tmp_x = w; w += nx_;
    bvec_t *tmp_z = w; w += nz_;
    bvec_t *tmp_rx = w; w += nrx_;
    bvec_t *tmp_rz = w; w += nrz_;

    // Propagate forward
    const bvec_t** arg1 = arg+n_in();
    fill_n(arg1, static_cast<size_t>(DE_NUM_IN), nullptr);
    arg1[DE_X] = arg[INTEGRATOR_X0];
    arg1[DE_P] = arg[INTEGRATOR_P];
    bvec_t** res1 = res+n_out();
    fill_n(res1, static_cast<size_t>(DE_NUM_OUT), nullptr);
    res1[DE_ODE] = tmp_x;
    res1[DE_ALG] = tmp_z;
    oracle_(arg1, res1, iw, w, 0);
    if (arg[INTEGRATOR_X0]) {
      const bvec_t *tmp = arg[INTEGRATOR_X0];
      for (int i=0; i<nx_; ++i) tmp_x[i] |= *tmp++;
    }

    // "Solve" in order to resolve interdependencies (cf. Rootfinder)
    copy_n(tmp_x, nx_+nz_, w);
    fill_n(tmp_x, nx_+nz_, 0);
    sp_jac_dae_.spsolve(btf_jac_dae_, tmp_x, w, false);

    // Get xf and zf
    if (res[INTEGRATOR_XF]) copy_n(tmp_x, nx_, res[INTEGRATOR_XF]);
    if (res[INTEGRATOR_ZF]) copy_n(tmp_z, nz_, res[INTEGRATOR_ZF]);

    // Propagate to quadratures
    if (nq_>0 && res[INTEGRATOR_QF]) {
      arg1[DE_X] = tmp_x;
      arg1[DE_Z] = tmp_z;
      res1[DE_ODE] = res1[DE_ALG] = 0;
      res1[DE_QUAD] = res[INTEGRATOR_QF];
      oracle_(arg1, res1, iw, w, 0);
    }

    if (nrx_>0) {
      // Propagate through g
      fill_n(arg1, static_cast<size_t>(DE_NUM_IN), nullptr);
      arg1[DE_X] = tmp_x;
      arg1[DE_P] = arg[INTEGRATOR_P];
      arg1[DE_Z] = tmp_z;
      arg1[DE_RX] = arg[INTEGRATOR_X0];
      arg1[DE_RX] = arg[INTEGRATOR_RX0];
      arg1[DE_RP] = arg[INTEGRATOR_RP];
      fill_n(res1, static_cast<size_t>(DE_NUM_OUT), nullptr);
      res1[DE_RODE] = tmp_rx;
      res1[DE_RALG] = tmp_rz;
      oracle_(arg1, res1, iw, w, 0);
      if (arg[INTEGRATOR_RX0]) {
        const bvec_t *tmp = arg[INTEGRATOR_RX0];
        for (int i=0; i<nrx_; ++i) tmp_rx[i] |= *tmp++;
      }

      // "Solve" in order to resolve interdependencies (cf. Rootfinder)
      copy_n(tmp_rx, nrx_+nrz_, w);
      fill_n(tmp_rx, nrx_+nrz_, 0);
      sp_jac_rdae_.spsolve(btf_jac_rdae_, tmp_rx, w, false);

      // Get rxf and rzf
      if (res[INTEGRATOR_RXF]) copy_n(tmp_rx, nrx_, res[INTEGRATOR_RXF]);
      if (res[INTEGRATOR_RZF]) copy_n(tmp_rz, nrz_, res[INTEGRATOR_RZF]);

      // Propagate to quadratures
      if (nrq_>0 && res[INTEGRATOR_RQF]) {
        arg1[DE_RX] = tmp_rx;
        arg1[DE_RZ] = tmp_rz;
        res1[DE_RODE] = res1[DE_RALG] = 0;
        res1[DE_RQUAD] = res[INTEGRATOR_RQF];
        oracle_(arg1, res1, iw, w, 0);
      }
    }
    log("Integrator::sp_fwd", "end");
  }

  void Integrator::sp_rev(bvec_t** arg, bvec_t** res, int* iw, bvec_t* w, int mem) {
    log("Integrator::sp_rev", "begin");

    // Work vectors
    bvec_t** arg1 = arg+n_in();
    bvec_t** res1 = res+n_out();
    bvec_t *tmp_x = w; w += nx_;
    bvec_t *tmp_z = w; w += nz_;

    // Shorthands
    bvec_t* x0 = arg[INTEGRATOR_X0];
    bvec_t* p = arg[INTEGRATOR_P];
    bvec_t* xf = res[INTEGRATOR_XF];
    bvec_t* zf = res[INTEGRATOR_ZF];
    bvec_t* qf = res[INTEGRATOR_QF];

    // Propagate from outputs to state vectors
    if (xf) {
      copy_n(xf, nx_, tmp_x);
      fill_n(xf, nx_, 0);
    } else {
      fill_n(tmp_x, nx_, 0);
    }
    if (zf) {
      copy_n(zf, nz_, tmp_z);
      fill_n(zf, nz_, 0);
    } else {
      fill_n(tmp_z, nz_, 0);
    }

    if (nrx_>0) {
      // Work vectors
      bvec_t *tmp_rx = w; w += nrx_;
      bvec_t *tmp_rz = w; w += nrz_;

      // Shorthands
      bvec_t* rx0 = arg[INTEGRATOR_RX0];
      bvec_t* rp = arg[INTEGRATOR_RP];
      bvec_t* rxf = res[INTEGRATOR_RXF];
      bvec_t* rzf = res[INTEGRATOR_RZF];
      bvec_t* rqf = res[INTEGRATOR_RQF];

      // Propagate from outputs to state vectors
      if (rxf) {
        copy_n(rxf, nrx_, tmp_rx);
        fill_n(rxf, nrx_, 0);
      } else {
        fill_n(tmp_rx, nrx_, 0);
      }
      if (rzf) {
        copy_n(rzf, nrz_, tmp_rz);
        fill_n(rzf, nrz_, 0);
      } else {
        fill_n(tmp_rz, nrz_, 0);
      }

      // Get dependencies from backward quadratures
      fill_n(res1, static_cast<size_t>(DE_NUM_OUT), nullptr);
      fill_n(arg1, static_cast<size_t>(DE_NUM_IN), nullptr);
      res1[DE_RQUAD] = rqf;
      arg1[DE_X] = tmp_x;
      arg1[DE_Z] = tmp_z;
      arg1[DE_P] = p;
      arg1[DE_RX] = tmp_rx;
      arg1[DE_RZ] = tmp_rz;
      arg1[DE_RP] = rp;
      oracle_.rev(arg1, res1, iw, w, 0);

      // Propagate interdependencies
      fill_n(w, nrx_+nrz_, 0);
      sp_jac_rdae_.spsolve(btf_jac_rdae_, w, tmp_rx, true);
      copy_n(w, nrx_+nrz_, tmp_rx);

      // Direct dependency rx0 -> rxf
      if (rx0) for (int i=0; i<nrx_; ++i) rx0[i] |= tmp_rx[i];

      // Indirect dependency via g
      res1[DE_RODE] = tmp_rx;
      res1[DE_RALG] = tmp_rz;
      res1[DE_RQUAD] = 0;
      arg1[DE_RX] = rx0;
      arg1[DE_RZ] = 0; // arg[INTEGRATOR_RZ0] is a guess, no dependency
      oracle_.rev(arg1, res1, iw, w, 0);
    }

    // Get dependencies from forward quadratures
    fill_n(res1, static_cast<size_t>(DE_NUM_OUT), nullptr);
    fill_n(arg1, static_cast<size_t>(DE_NUM_IN), nullptr);
    res1[DE_QUAD] = qf;
    arg1[DE_X] = tmp_x;
    arg1[DE_Z] = tmp_z;
    arg1[DE_P] = p;
    if (qf && nq_>0) oracle_.rev(arg1, res1, iw, w, 0);

    // Propagate interdependencies
    fill_n(w, nx_+nz_, 0);
    sp_jac_dae_.spsolve(btf_jac_dae_, w, tmp_x, true);
    copy_n(w, nx_+nz_, tmp_x);

    // Direct dependency x0 -> xf
    if (x0) for (int i=0; i<nx_; ++i) x0[i] |= tmp_x[i];

    // Indirect dependency through f
    res1[DE_ODE] = tmp_x;
    res1[DE_ALG] = tmp_z;
    res1[DE_QUAD] = 0;
    arg1[DE_X] = x0;
    arg1[DE_Z] = 0; // arg[INTEGRATOR_Z0] is a guess, no dependency
    oracle_.rev(arg1, res1, iw, w, 0);

    log("Integrator::sp_rev", "end");
  }

  Integrator::AugOffset Integrator::getAugOffset(int nfwd, int nadj) {
    // Form return object
    AugOffset ret;
    ret.x.resize(1, 0);
    ret.z.resize(1, 0);
    ret.q.resize(1, 0);
    ret.p.resize(1, 0);
    ret.rx.resize(1, 0);
    ret.rz.resize(1, 0);
    ret.rq.resize(1, 0);
    ret.rp.resize(1, 0);

    // Count nondifferentiated and forward sensitivities
    for (int dir=-1; dir<nfwd; ++dir) {
      if ( nx_>0) ret.x.push_back(x().size2());
      if ( nz_>0) ret.z.push_back(z().size2());
      if ( nq_>0) ret.q.push_back(q().size2());
      if ( np_>0) ret.p.push_back(p().size2());
      if (nrx_>0) ret.rx.push_back(rx().size2());
      if (nrz_>0) ret.rz.push_back(rz().size2());
      if (nrq_>0) ret.rq.push_back(rq().size2());
      if (nrp_>0) ret.rp.push_back(rp().size2());
    }

    // Count adjoint sensitivities
    for (int dir=0; dir<nadj; ++dir) {
      if ( nx_>0) ret.rx.push_back(x().size2());
      if ( nz_>0) ret.rz.push_back(z().size2());
      if ( np_>0) ret.rq.push_back(p().size2());
      if ( nq_>0) ret.rp.push_back(q().size2());
      if (nrx_>0) ret.x.push_back(x().size2());
      if (nrz_>0) ret.z.push_back(rz().size2());
      if (nrp_>0) ret.q.push_back(rp().size2());
      if (nrq_>0) ret.p.push_back(rq().size2());
    }

    // Get cummulative offsets
    for (int i=1; i<ret.x.size(); ++i) ret.x[i] += ret.x[i-1];
    for (int i=1; i<ret.z.size(); ++i) ret.z[i] += ret.z[i-1];
    for (int i=1; i<ret.q.size(); ++i) ret.q[i] += ret.q[i-1];
    for (int i=1; i<ret.p.size(); ++i) ret.p[i] += ret.p[i-1];
    for (int i=1; i<ret.rx.size(); ++i) ret.rx[i] += ret.rx[i-1];
    for (int i=1; i<ret.rz.size(); ++i) ret.rz[i] += ret.rz[i-1];
    for (int i=1; i<ret.rq.size(); ++i) ret.rq[i] += ret.rq[i-1];
    for (int i=1; i<ret.rp.size(); ++i) ret.rp[i] += ret.rp[i-1];

    // Return the offsets
    return ret;
  }

  Function Integrator::get_forward_old(const std::string& name, int nfwd, Dict& opts) {
    log("Integrator::get_forward", "begin");

    // Integrator options
    Dict aug_opts = getDerivativeOptions(true);
    for (auto&& i : augmented_options_) {
      aug_opts[i.first] = i.second;
    }

    // Temp stringstream
    string iname = "aug_f" + to_string(nfwd) + name_;

    // Create integrator for augmented DAE
    Function aug_int;
    if (oracle_.is_a("sxfunction")) {
      aug_int = integrator(iname, plugin_name(), aug_fwd<SX>(nfwd), aug_opts);
    } else {
      aug_int = integrator(iname, plugin_name(), aug_fwd<MX>(nfwd), aug_opts);
    }

    // All inputs of the return function
    vector<MX> ret_in;
    ret_in.reserve(INTEGRATOR_NUM_IN*(1+nfwd) + INTEGRATOR_NUM_OUT);

    // Augmented state
    vector<MX> x0_augv, p_augv, z0_augv, rx0_augv, rp_augv, rz0_augv;

    // Add nondifferentiated inputs and forward seeds
    for (int dir=-1; dir<nfwd; ++dir) {
      // Suffix
      string suff;
      if (dir>=0) suff = "_" + to_string(dir);

      // Augmented problem
      vector<MX> din(INTEGRATOR_NUM_IN);
      x0_augv.push_back(din[INTEGRATOR_X0] = MX::sym("x0" + suff, x()));
      p_augv.push_back(din[INTEGRATOR_P] = MX::sym("p" + suff, p()));
      z0_augv.push_back(din[INTEGRATOR_Z0] = MX::sym("z0" + suff, z()));
      rx0_augv.push_back(din[INTEGRATOR_RX0] = MX::sym("rx0" + suff, rx()));
      rp_augv.push_back(din[INTEGRATOR_RP] = MX::sym("rp" + suff, rp()));
      rz0_augv.push_back(din[INTEGRATOR_RZ0] = MX::sym("rz0" + suff, rz()));
      ret_in.insert(ret_in.end(), din.begin(), din.end());

      // Dummy outputs
      if (dir==-1) {
        vector<MX> dout(INTEGRATOR_NUM_OUT);
        dout[INTEGRATOR_XF]  = MX::sym("xf_dummy", Sparsity(size_out(INTEGRATOR_XF)));
        dout[INTEGRATOR_QF]  = MX::sym("qf_dummy", Sparsity(q().size()));
        dout[INTEGRATOR_ZF]  = MX::sym("zf_dummy", Sparsity(z().size()));
        dout[INTEGRATOR_RXF]  = MX::sym("rxf_dummy", Sparsity(rx().size()));
        dout[INTEGRATOR_RQF]  = MX::sym("rqf_dummy", Sparsity(rq().size()));
        dout[INTEGRATOR_RZF]  = MX::sym("rzf_dummy", Sparsity(rz().size()));
        ret_in.insert(ret_in.end(), dout.begin(), dout.end());
      }
    }

    // Call the integrator
    vector<MX> integrator_in(INTEGRATOR_NUM_IN);
    integrator_in[INTEGRATOR_X0] = horzcat(x0_augv);
    integrator_in[INTEGRATOR_P] = horzcat(p_augv);
    integrator_in[INTEGRATOR_Z0] = horzcat(z0_augv);
    integrator_in[INTEGRATOR_RX0] = horzcat(rx0_augv);
    integrator_in[INTEGRATOR_RP] = horzcat(rp_augv);
    integrator_in[INTEGRATOR_RZ0] = horzcat(rz0_augv);
    vector<MX> integrator_out = aug_int(integrator_in);

    // Augmented results
    AugOffset offset = getAugOffset(nfwd, 0);
    vector<MX> xf_aug = horzsplit(integrator_out[INTEGRATOR_XF], offset.x);
    vector<MX> qf_aug = horzsplit(integrator_out[INTEGRATOR_QF], offset.q);
    vector<MX> zf_aug = horzsplit(integrator_out[INTEGRATOR_ZF], offset.z);
    vector<MX> rxf_aug = horzsplit(integrator_out[INTEGRATOR_RXF], offset.rx);
    vector<MX> rqf_aug = horzsplit(integrator_out[INTEGRATOR_RQF], offset.rq);
    vector<MX> rzf_aug = horzsplit(integrator_out[INTEGRATOR_RZF], offset.rz);
    vector<MX>::const_iterator xf_aug_it = xf_aug.begin();
    vector<MX>::const_iterator qf_aug_it = qf_aug.begin();
    vector<MX>::const_iterator zf_aug_it = zf_aug.begin();
    vector<MX>::const_iterator rxf_aug_it = rxf_aug.begin();
    vector<MX>::const_iterator rqf_aug_it = rqf_aug.begin();
    vector<MX>::const_iterator rzf_aug_it = rzf_aug.begin();

    // All outputs of the return function
    vector<MX> ret_out;
    ret_out.reserve(INTEGRATOR_NUM_OUT*nfwd);

    // Collect the forward sensitivities
    vector<MX> dd(INTEGRATOR_NUM_IN);
    for (int dir=-1; dir<nfwd; ++dir) {
      if ( nx_>0) dd[INTEGRATOR_XF]  = *xf_aug_it++;
      if ( nq_>0) dd[INTEGRATOR_QF]  = *qf_aug_it++;
      if ( nz_>0) dd[INTEGRATOR_ZF]  = *zf_aug_it++;
      if (nrx_>0) dd[INTEGRATOR_RXF] = *rxf_aug_it++;
      if (nrq_>0) dd[INTEGRATOR_RQF] = *rqf_aug_it++;
      if (nrz_>0) dd[INTEGRATOR_RZF] = *rzf_aug_it++;
      if (dir>=0) // Nondifferentiated output ignored
        ret_out.insert(ret_out.end(), dd.begin(), dd.end());
    }
    log("Integrator::get_forward", "end");

    // Create derivative function and return
    return Function(name, ret_in, ret_out, opts);
  }

  Function Integrator::get_reverse_old(const std::string& name, int nadj, Dict& opts) {
    log("Integrator::get_reverse", "begin");

    // Integrator options
    Dict aug_opts = getDerivativeOptions(false);
    for (auto&& i : augmented_options_) {
      aug_opts[i.first] = i.second;
    }

    // Temp stringstream
    stringstream ss;
    ss << "aug_r" << nadj << name_;

    // Create integrator for augmented DAE
    Function aug_int;
    AugOffset offset = getAugOffset(0, nadj);
    if (oracle_.is_a("sxfunction")) {
      aug_int = integrator(ss.str(), plugin_name(), aug_adj<SX>(nadj), aug_opts);
    } else {
      aug_int = integrator(ss.str(), plugin_name(), aug_adj<MX>(nadj), aug_opts);
    }

    // All inputs of the return function
    vector<MX> ret_in;
    ret_in.reserve(INTEGRATOR_NUM_IN + INTEGRATOR_NUM_OUT*(1+nadj));

    // Augmented state
    vector<MX> x0_augv, p_augv, z0_augv, rx0_augv, rp_augv, rz0_augv;

    // Inputs or forward/adjoint seeds in one direction
    vector<MX> dd;

    // Add nondifferentiated inputs and forward seeds
    dd.resize(INTEGRATOR_NUM_IN);
    fill(dd.begin(), dd.end(), MX());

    // Differential state
    dd[INTEGRATOR_X0] = MX::sym("x0", x());
    x0_augv.push_back(dd[INTEGRATOR_X0]);

    // Parameter
    dd[INTEGRATOR_P] = MX::sym("p", p());
    p_augv.push_back(dd[INTEGRATOR_P]);

    // Initial guess for algebraic variable
    dd[INTEGRATOR_Z0] = MX::sym("r0", z());
    z0_augv.push_back(dd[INTEGRATOR_Z0]);

    // Backward state
    dd[INTEGRATOR_RX0] = MX::sym("rx0", rx());
    rx0_augv.push_back(dd[INTEGRATOR_RX0]);

    // Backward parameter
    dd[INTEGRATOR_RP] = MX::sym("rp", rp());
    rp_augv.push_back(dd[INTEGRATOR_RP]);

    // Initial guess for backward algebraic variable
    dd[INTEGRATOR_RZ0] = MX::sym("rz0", rz());
    rz0_augv.push_back(dd[INTEGRATOR_RZ0]);

    // Add to input vector
    ret_in.insert(ret_in.end(), dd.begin(), dd.end());

    // Add dummy inputs (outputs of the nondifferentiated funciton)
    dd.resize(INTEGRATOR_NUM_OUT);
    dd[INTEGRATOR_XF]  = MX::sym("xf_dummy", Sparsity(x().size()));
    dd[INTEGRATOR_QF]  = MX::sym("qf_dummy", Sparsity(q().size()));
    dd[INTEGRATOR_ZF]  = MX::sym("zf_dummy", Sparsity(z().size()));
    dd[INTEGRATOR_RXF]  = MX::sym("rxf_dummy", Sparsity(rx().size()));
    dd[INTEGRATOR_RQF]  = MX::sym("rqf_dummy", Sparsity(rq().size()));
    dd[INTEGRATOR_RZF]  = MX::sym("rzf_dummy", Sparsity(rz().size()));
    ret_in.insert(ret_in.end(), dd.begin(), dd.end());

    // Add adjoint seeds
    dd.resize(INTEGRATOR_NUM_OUT);
    fill(dd.begin(), dd.end(), MX());
    for (int dir=0; dir<nadj; ++dir) {

      // Differential states become backward differential state
      ss.clear();
      ss << "xf" << "_" << dir;
      dd[INTEGRATOR_XF] = MX::sym(ss.str(), x());
      rx0_augv.push_back(dd[INTEGRATOR_XF]);

      // Quadratures become backward parameters
      ss.clear();
      ss << "qf" << "_" << dir;
      dd[INTEGRATOR_QF] = MX::sym(ss.str(), q());
      rp_augv.push_back(dd[INTEGRATOR_QF]);

      // Algebraic variables become backward algebraic variables
      ss.clear();
      ss << "zf" << "_" << dir;
      dd[INTEGRATOR_ZF] = MX::sym(ss.str(), z());
      rz0_augv.push_back(dd[INTEGRATOR_ZF]);

      // Backward differential states becomes forward differential states
      ss.clear();
      ss << "rxf" << "_" << dir;
      dd[INTEGRATOR_RXF] = MX::sym(ss.str(), rx());
      x0_augv.push_back(dd[INTEGRATOR_RXF]);

      // Backward quadratures becomes (forward) parameters
      ss.clear();
      ss << "rqf" << "_" << dir;
      dd[INTEGRATOR_RQF] = MX::sym(ss.str(), rq());
      p_augv.push_back(dd[INTEGRATOR_RQF]);

      // Backward differential states becomes forward differential states
      ss.clear();
      ss << "rzf" << "_" << dir;
      dd[INTEGRATOR_RZF] = MX::sym(ss.str(), rz());
      z0_augv.push_back(dd[INTEGRATOR_RZF]);

      // Add to input vector
      ret_in.insert(ret_in.end(), dd.begin(), dd.end());
    }

    // Call the integrator
    vector<MX> integrator_in(INTEGRATOR_NUM_IN);
    integrator_in[INTEGRATOR_X0] = horzcat(x0_augv);
    integrator_in[INTEGRATOR_P] = horzcat(p_augv);
    integrator_in[INTEGRATOR_Z0] = horzcat(z0_augv);
    integrator_in[INTEGRATOR_RX0] = horzcat(rx0_augv);
    integrator_in[INTEGRATOR_RP] = horzcat(rp_augv);
    integrator_in[INTEGRATOR_RZ0] = horzcat(rz0_augv);
    vector<MX> integrator_out = aug_int(integrator_in);

    // Augmented results
    vector<MX> xf_aug = horzsplit(integrator_out[INTEGRATOR_XF], offset.x);
    vector<MX> qf_aug = horzsplit(integrator_out[INTEGRATOR_QF], offset.q);
    vector<MX> zf_aug = horzsplit(integrator_out[INTEGRATOR_ZF], offset.z);
    vector<MX> rxf_aug = horzsplit(integrator_out[INTEGRATOR_RXF], offset.rx);
    vector<MX> rqf_aug = horzsplit(integrator_out[INTEGRATOR_RQF], offset.rq);
    vector<MX> rzf_aug = horzsplit(integrator_out[INTEGRATOR_RZF], offset.rz);
    vector<MX>::const_iterator xf_aug_it = xf_aug.begin();
    vector<MX>::const_iterator qf_aug_it = qf_aug.begin();
    vector<MX>::const_iterator zf_aug_it = zf_aug.begin();
    vector<MX>::const_iterator rxf_aug_it = rxf_aug.begin();
    vector<MX>::const_iterator rqf_aug_it = rqf_aug.begin();
    vector<MX>::const_iterator rzf_aug_it = rzf_aug.begin();

    // All outputs of the return function
    vector<MX> ret_out;
    ret_out.reserve(INTEGRATOR_NUM_IN*nadj);

    // Collect the nondifferentiated results and forward sensitivities
    dd.resize(INTEGRATOR_NUM_OUT);
    fill(dd.begin(), dd.end(), MX());
    for (int dir=-1; dir<0; ++dir) {
      if ( nx_>0) dd[INTEGRATOR_XF]  = *xf_aug_it++;
      if ( nq_>0) dd[INTEGRATOR_QF]  = *qf_aug_it++;
      if ( nz_>0) dd[INTEGRATOR_ZF]  = *zf_aug_it++;
      if (nrx_>0) dd[INTEGRATOR_RXF] = *rxf_aug_it++;
      if (nrq_>0) dd[INTEGRATOR_RQF] = *rqf_aug_it++;
      if (nrz_>0) dd[INTEGRATOR_RZF] = *rzf_aug_it++;
      //ret_out.insert(ret_out.end(), dd.begin(), dd.end());
    }

    // Collect the adjoint sensitivities
    dd.resize(INTEGRATOR_NUM_IN);
    fill(dd.begin(), dd.end(), MX());
    for (int dir=0; dir<nadj; ++dir) {
      if ( nx_>0) dd[INTEGRATOR_X0]  = *rxf_aug_it++;
      if ( np_>0) dd[INTEGRATOR_P]   = *rqf_aug_it++;
      if ( nz_>0) dd[INTEGRATOR_Z0]  = *rzf_aug_it++;
      if (nrx_>0) dd[INTEGRATOR_RX0] = *xf_aug_it++;
      if (nrp_>0) dd[INTEGRATOR_RP]  = *qf_aug_it++;
      if (nrz_>0) dd[INTEGRATOR_RZ0] = *zf_aug_it++;
      ret_out.insert(ret_out.end(), dd.begin(), dd.end());
    }
    log("Integrator::getDerivative", "end");

    // Create derivative function and return
    return Function(name, ret_in, ret_out, opts);
  }

  void Integrator::set_temp(void* mem, const double** arg, double** res,
                            int* iw, double* w) const {
    auto m = static_cast<IntegratorMemory*>(mem);
    m->arg = arg;
    m->res = res;
    m->iw = iw;
    m->w = w;
  }

  Dict Integrator::getDerivativeOptions(bool fwd) {
    // Copy all options
    return opts_;
  }

  Sparsity Integrator::sp_jac_dae() {
    // Start with the sparsity pattern of the ODE part
    Sparsity jac_ode_x = oracle_.sparsity_jac(DE_X, DE_ODE);

    // Add diagonal to get interdependencies
    jac_ode_x = jac_ode_x + Sparsity::diag(nx_);

    // Quick return if no algebraic variables
    if (nz_==0) return jac_ode_x;

    // Add contribution from algebraic variables and equations
    Sparsity jac_ode_z = oracle_.sparsity_jac(DE_Z, DE_ODE);
    Sparsity jac_alg_x = oracle_.sparsity_jac(DE_X, DE_ALG);
    Sparsity jac_alg_z = oracle_.sparsity_jac(DE_Z, DE_ALG);
    return blockcat(jac_ode_x, jac_ode_z,
                    jac_alg_x, jac_alg_z);
  }

  Sparsity Integrator::sp_jac_rdae() {
    // Start with the sparsity pattern of the ODE part
    Sparsity jac_ode_x = oracle_.sparsity_jac(DE_RX, DE_RODE);

    // Add diagonal to get interdependencies
    jac_ode_x = jac_ode_x + Sparsity::diag(nrx_);

    // Quick return if no algebraic variables
    if (nrz_==0) return jac_ode_x;

    // Add contribution from algebraic variables and equations
    Sparsity jac_ode_z = oracle_.sparsity_jac(DE_RZ, DE_RODE);
    Sparsity jac_alg_x = oracle_.sparsity_jac(DE_RX, DE_RALG);
    Sparsity jac_alg_z = oracle_.sparsity_jac(DE_RZ, DE_RALG);
    return blockcat(jac_ode_x, jac_ode_z,
                    jac_alg_x, jac_alg_z);
  }

  std::map<std::string, Integrator::Plugin> Integrator::solvers_;

  const std::string Integrator::infix_ = "integrator";

  void Integrator::setStopTime(IntegratorMemory* mem, double tf) const {
    casadi_error("Integrator::setStopTime not defined for class "
                 << typeid(*this).name());
  }

  FixedStepIntegrator::FixedStepIntegrator(const std::string& name, const Function& dae)
    : Integrator(name, dae) {

    // Default options
    nk_ = 20;
  }

  FixedStepIntegrator::~FixedStepIntegrator() {
    clear_memory();
  }

  Options FixedStepIntegrator::options_
  = {{&Integrator::options_},
     {{"number_of_finite_elements",
       {OT_INT,
        "Number of finite elements"}}
     }
  };

  void FixedStepIntegrator::init(const Dict& opts) {
    // Call the base class init
    Integrator::init(opts);

    // Read options
    for (auto&& op : opts) {
      if (op.first=="number_of_finite_elements") {
        nk_ = op.second;
      }
    }

    // Number of finite elements and time steps
    casadi_assert(nk_>0);
    h_ = (grid_.back() - grid_.front())/nk_;

    // Setup discrete time dynamics
    setupFG();

    // Get discrete time dimensions
    nZ_ = F_.nnz_in(DAE_Z);
    nRZ_ =  G_.is_null() ? 0 : G_.nnz_in(RDAE_RZ);
  }

  void FixedStepIntegrator::init_memory(void* mem) const {
    Integrator::init_memory(mem);
    auto m = static_cast<FixedStepMemory*>(mem);

    // Discrete time algebraic variable
    m->Z = DM::zeros(F_.sparsity_in(DAE_Z));
    m->RZ = G_.is_null() ? DM() : DM::zeros(G_.sparsity_in(RDAE_RZ));

    // Allocate tape if backward states are present
    if (nrx_>0) {
      m->x_tape.resize(nk_+1, vector<double>(nx_));
      m->Z_tape.resize(nk_, vector<double>(nZ_));
    }

    // Allocate state
    m->x.resize(nx_);
    m->z.resize(nz_);
    m->p.resize(np_);
    m->q.resize(nq_);
    m->rx.resize(nrx_);
    m->rz.resize(nrz_);
    m->rp.resize(nrp_);
    m->rq.resize(nrq_);
    m->x_prev.resize(nx_);
    m->Z_prev.resize(nZ_);
    m->q_prev.resize(nq_);
    m->rx_prev.resize(nrx_);
    m->RZ_prev.resize(nRZ_);
    m->rq_prev.resize(nrq_);
  }

  void FixedStepIntegrator::advance(IntegratorMemory* mem, double t,
                                    double* x, double* z, double* q) const {
    auto m = static_cast<FixedStepMemory*>(mem);

    // Get discrete time sought
    int k_out = std::ceil((t - grid_.front())/h_);
    k_out = std::min(k_out, nk_); //  make sure that rounding errors does not result in k_out>nk_
    casadi_assert(k_out>=0);

    // Explicit discrete time dynamics
    const Function& F = getExplicit();

    // Discrete dynamics function inputs ...
    fill_n(m->arg, F.n_in(), nullptr);
    m->arg[DAE_T] = &m->t;
    m->arg[DAE_X] = get_ptr(m->x_prev);
    m->arg[DAE_Z] = get_ptr(m->Z_prev);
    m->arg[DAE_P] = get_ptr(m->p);

    // ... and outputs
    fill_n(m->res, F.n_out(), nullptr);
    m->res[DAE_ODE] = get_ptr(m->x);
    m->res[DAE_ALG] = get_ptr(m->Z);
    m->res[DAE_QUAD] = get_ptr(m->q);

    // Take time steps until end time has been reached
    while (m->k<k_out) {
      // Update the previous step
      casadi_copy(get_ptr(m->x), nx_, get_ptr(m->x_prev));
      casadi_copy(get_ptr(m->Z), nZ_, get_ptr(m->Z_prev));
      casadi_copy(get_ptr(m->q), nq_, get_ptr(m->q_prev));

      // Take step
      F(m->arg, m->res, m->iw, m->w, 0);
      casadi_axpy(nq_, 1., get_ptr(m->q_prev), get_ptr(m->q));

      // Tape
      if (nrx_>0) {
        casadi_copy(get_ptr(m->x), nx_, get_ptr(m->x_tape.at(m->k+1)));
        casadi_copy(get_ptr(m->Z), m->Z.nnz(), get_ptr(m->Z_tape.at(m->k)));
      }

      // Advance time
      m->k++;
      m->t = grid_.front() + m->k*h_;
    }

    // Return to user TODO(@jaeandersson): interpolate
    casadi_copy(get_ptr(m->x), nx_, x);
    casadi_copy(get_ptr(m->Z)+m->Z.nnz()-nz_, nz_, z);
    casadi_copy(get_ptr(m->q), nq_, q);
  }

  void FixedStepIntegrator::retreat(IntegratorMemory* mem, double t,
                                    double* rx, double* rz, double* rq) const {
    auto m = static_cast<FixedStepMemory*>(mem);

    // Get discrete time sought
    int k_out = std::floor((t - grid_.front())/h_);
    k_out = std::max(k_out, 0); //  make sure that rounding errors does not result in k_out>nk_
    casadi_assert(k_out<=nk_);

    // Explicit discrete time dynamics
    const Function& G = getExplicitB();

    // Discrete dynamics function inputs ...
    fill_n(m->arg, G.n_in(), nullptr);
    m->arg[RDAE_T] = &m->t;
    m->arg[RDAE_P] = get_ptr(m->p);
    m->arg[RDAE_RX] = get_ptr(m->rx_prev);
    m->arg[RDAE_RZ] = get_ptr(m->RZ_prev);
    m->arg[RDAE_RP] = get_ptr(m->rp);

    // ... and outputs
    fill_n(m->res, G.n_out(), nullptr);
    m->res[RDAE_ODE] = get_ptr(m->rx);
    m->res[RDAE_ALG] = get_ptr(m->RZ);
    m->res[RDAE_QUAD] = get_ptr(m->rq);

    // Take time steps until end time has been reached
    while (m->k>k_out) {
      // Advance time
      m->k--;
      m->t = grid_.front() + m->k*h_;

      // Update the previous step
      casadi_copy(get_ptr(m->rx), nrx_, get_ptr(m->rx_prev));
      casadi_copy(get_ptr(m->RZ), nRZ_, get_ptr(m->RZ_prev));
      casadi_copy(get_ptr(m->rq), nrq_, get_ptr(m->rq_prev));

      // Take step
      m->arg[RDAE_X] = get_ptr(m->x_tape.at(m->k));
      m->arg[RDAE_Z] = get_ptr(m->Z_tape.at(m->k));
      G(m->arg, m->res, m->iw, m->w, 0);
      casadi_axpy(nrq_, 1., get_ptr(m->rq_prev), get_ptr(m->rq));
    }

    // Return to user TODO(@jaeandersson): interpolate
    casadi_copy(get_ptr(m->rx), nrx_, rx);
    casadi_copy(get_ptr(m->RZ)+m->RZ.nnz()-nrz_, nrz_, rz);
    casadi_copy(get_ptr(m->rq), nrq_, rq);
  }

  void FixedStepIntegrator::
  reset(IntegratorMemory* mem, double t,
        const double* x, const double* z, const double* p) const {
    auto m = static_cast<FixedStepMemory*>(mem);

    // Update time
    m->t = t;

    // Set parameters
    casadi_copy(p, np_, get_ptr(m->p));

    // Update the state
    casadi_copy(x, nx_, get_ptr(m->x));
    casadi_copy(z, nz_, get_ptr(m->z));

    // Reset summation states
    casadi_fill(get_ptr(m->q), nq_, 0.);

    // Bring discrete time to the beginning
    m->k = 0;

    // Get consistent initial conditions
    casadi_fill(m->Z.ptr(), m->Z.nnz(), numeric_limits<double>::quiet_NaN());

    // Add the first element in the tape
    if (nrx_>0) {
      casadi_copy(x, nx_, get_ptr(m->x_tape.at(0)));
    }
  }

  void FixedStepIntegrator::resetB(IntegratorMemory* mem, double t, const double* rx,
                                   const double* rz, const double* rp) const {
    auto m = static_cast<FixedStepMemory*>(mem);

    // Update time
    m->t = t;

    // Set parameters
    casadi_copy(rp, nrp_, get_ptr(m->rp));

    // Update the state
    casadi_copy(rx, nrx_, get_ptr(m->rx));
    casadi_copy(rz, nrz_, get_ptr(m->rz));

    // Reset summation states
    casadi_fill(get_ptr(m->rq), nrq_, 0.);

    // Bring discrete time to the end
    m->k = nk_;

    // Get consistent initial conditions
    casadi_fill(m->RZ.ptr(), m->RZ.nnz(), numeric_limits<double>::quiet_NaN());
  }

  ImplicitFixedStepIntegrator::
  ImplicitFixedStepIntegrator(const std::string& name, const Function& dae)
    : FixedStepIntegrator(name, dae) {
  }

  ImplicitFixedStepIntegrator::~ImplicitFixedStepIntegrator() {
  }

  Options ImplicitFixedStepIntegrator::options_
  = {{&FixedStepIntegrator::options_},
     {{"rootfinder",
       {OT_STRING,
        "An implicit function solver"}},
      {"rootfinder_options",
       {OT_DICT,
        "Options to be passed to the NLP Solver"}}
     }
  };

  void ImplicitFixedStepIntegrator::init(const Dict& opts) {
    // Call the base class init
    FixedStepIntegrator::init(opts);

    // Default (temporary) options
    std::string implicit_function_name = "newton";
    Dict rootfinder_options;

    // Read options
    for (auto&& op : opts) {
      if (op.first=="rootfinder") {
        implicit_function_name = op.second.to_string();
      } else if (op.first=="rootfinder_options") {
        rootfinder_options = op.second;
      }
    }

    // Complete rootfinder dictionary
    rootfinder_options["implicit_input"] = DAE_Z;
    rootfinder_options["implicit_output"] = DAE_ALG;

    // Allocate a solver
    rootfinder_ = rootfinder(name_ + "_rootfinder", implicit_function_name,
                                  F_, rootfinder_options);
    alloc(rootfinder_);

    // Allocate a root-finding solver for the backward problem
    if (nRZ_>0) {
      // Options
      Dict backward_rootfinder_options = rootfinder_options;
      backward_rootfinder_options["implicit_input"] = RDAE_RZ;
      backward_rootfinder_options["implicit_output"] = RDAE_ALG;
      string backward_implicit_function_name = implicit_function_name;

      // Allocate a Newton solver
      backward_rootfinder_ =
        rootfinder(name_+ "_backward_rootfinder",
                   backward_implicit_function_name,
                   G_, backward_rootfinder_options);
      alloc(backward_rootfinder_);
    }
  }

} // namespace casadi
