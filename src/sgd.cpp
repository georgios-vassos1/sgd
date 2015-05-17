// -*- mode: C++; c-indent-level: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*-

#include "basedef.h"
#include "data.h"
#include "experiment.h"
#include "glm-family.h"
#include "glm-transfer.h"
#include "learningrate.h"
#include <stdlib.h>

// Auxiliary function
template<typename EXPERIMENT>
Rcpp::List run_experiment(SEXP dataset, SEXP method, SEXP verbose, EXPERIMENT exprm, Rcpp::List Experiment);

// via the depends attribute we tell Rcpp to create hooks for
// RcppArmadillo so that the build process will know what to do
// This file will be compiled with C++11
// BH provides methods to use boost library
//
// [[Rcpp::depends(RcppArmadillo)]]
// [[Rcpp::plugins(cpp11)]]
// [[Rcpp::depends(BH)]]

//return the nsamples and nfeatures of a dataset
Sgd_Size Sgd_dataset_size(const Sgd_Dataset& dataset) {
  Sgd_Size size;
  size.nsamples = dataset.X.n_rows;
  size.d = dataset.X.n_cols;
  return size;
}

// return the @t th data point in @dataset
Sgd_DataPoint Sgd_get_dataset_point(const Sgd_Dataset& dataset, unsigned t) {
  t = t - 1;
  mat xt = mat(dataset.X.row(t));
  double yt = dataset.Y(t);
  return Sgd_DataPoint(xt, yt);
}

// return the new estimate of parameters, using SGD
template<typename EXPERIMENT>
mat Sgd_sgd_online_algorithm(unsigned t, const mat& theta_old,
  const Sgd_Dataset& data_history, const EXPERIMENT& experiment,
  bool& good_gradient) {

  Sgd_DataPoint datapoint = Sgd_get_dataset_point(data_history, t);
  // mat theta_old = Sgd_onlineOutput_estimate(online_out, t-1);
  Sgd_Learn_Rate_Value at = experiment.learning_rate(theta_old, datapoint, experiment.offset[t-1], t);
  mat grad_t = experiment.gradient(theta_old, datapoint, experiment.offset[t-1]);
  if (!is_finite(grad_t)) {
    good_gradient = false;
  }
#if DEBUG
  static int count = 0;
  if (count < 10) {
    Rcpp::Rcout << "learning rate: \n" << at << std::endl;
    Rcpp::Rcout << "gradient: \n" << grad_t << std::endl;
  }
    ++count;
#endif
  mat theta_new = theta_old + (at * grad_t);
  return theta_new;
}

// return the new estimate of parameters, using implicit SGD
// TODO add per model
mat Sgd_implicit_online_algorithm(unsigned t, const mat& theta_old,
  const Sgd_Dataset& data_history, const Sgd_Experiment_Glm& experiment,
  bool& good_gradient) {

  Sgd_DataPoint datapoint= Sgd_get_dataset_point(data_history, t);
  // mat theta_old = Sgd_onlineOutput_estimate(online_out, t-1);
  mat theta_new;
  Sgd_Learn_Rate_Value at = experiment.learning_rate(theta_old, datapoint, experiment.offset[t-1], t);
  double average_lr = 0;
  if (at.type == 0) average_lr = at.lr_scalar;
  else {
    vec diag_lr = at.lr_mat.diag();
    for (unsigned i = 0; i < diag_lr.n_elem; ++i) {
      average_lr += diag_lr[i];
    }
    average_lr /= diag_lr.n_elem;
  }

  double normx = dot(datapoint.x, datapoint.x);

  Get_grad_coeff<Sgd_Experiment_Glm> get_grad_coeff(experiment, datapoint, theta_old, normx, experiment.offset[t-1]);
  Implicit_fn<Sgd_Experiment_Glm> implicit_fn(average_lr, get_grad_coeff);

  double rt = average_lr * get_grad_coeff(0);
  double lower = 0;
  double upper = 0;
  if (rt < 0) {
    upper = 0;
    lower = rt;
  }
  else {
    double u = 0;
    u = (experiment.g_link(datapoint.y) - dot(theta_old,datapoint.x))/normx;
    upper = std::min(rt, u);
    lower = 0;
  }
  double result;
  if (lower != upper) {
    result = boost::math::tools::schroeder_iterate(implicit_fn, (lower + upper)/2, lower, upper, 14);
  }
  else
    result = lower;
  theta_new = theta_old + result * datapoint.x.t();
  return theta_new;
}

// TODO add per model
bool validity_check(const Sgd_Dataset& data, const mat& theta, unsigned t, const Sgd_Experiment_Glm& exprm) {
  //check if all estimates are finite
  if (!is_finite(theta)) {
    Rcpp::Rcout << "warning: non-finite coefficients at iteration " << t << std::endl;
  }

  //check if eta is in the support
  double eta = exprm.offset[t-1] + dot(Sgd_get_dataset_point(data, t).x, theta);
  if (!exprm.valideta(eta)) {
    Rcpp::Rcout << "no valid set of coefficients has been found: please supply starting values" << t << std::endl;
    return false;
  }

  //check the variance of the expectation of Y
  double mu_var = exprm.variance(exprm.h_transfer(eta));
  if (!is_finite(mu_var)) {
    Rcpp::Rcout << "NA in V(mu) in iteration " << t << std::endl;
    Rcpp::Rcout << "current theta: " << theta << std::endl;
    Rcpp::Rcout << "current eta: " << eta << std::endl;
    return false;
  }
  if (mu_var == 0) {
    Rcpp::Rcout << "0 in V(mu) in iteration" << t << std::endl;
    Rcpp::Rcout << "current theta: " << theta << std::endl;
    Rcpp::Rcout << "current eta: " << eta << std::endl;
    return false;
  }
  double deviance = 0;
  mat mu;
  mat eta_mat;
  //check the deviance
  if (exprm.dev) {
    eta_mat = data.X * theta + exprm.offset;
    mu = exprm.h_transfer(eta_mat);
    deviance = exprm.deviance(data.Y, mu, exprm.weights);
    if(!is_finite(deviance)) {
      Rcpp::Rcout << "Deviance is non-finite" << std::endl;
      return false;
    }
  }
  //print if trace
  if (exprm.trace) {
    if (!exprm.dev) {
      eta_mat = data.X * theta + exprm.offset;
      mu = exprm.h_transfer(eta_mat);
      deviance = exprm.deviance(data.Y, mu, exprm.weights);
    }
    Rcpp::Rcout << "Deviance = " << deviance << " , Iterations - " << t << std::endl;
  }
  return true;
}

template<typename EXPERIMENT>
Rcpp::List post_process_glm(const Sgd_OnlineOutput& out, const Sgd_Dataset& data,
  const EXPERIMENT& exprm, mat& coef, unsigned X_rank) {
  //check the validity of eta for all observations
  mat eta;
  eta = data.X * out.last_estimate() + exprm.offset;
  mat mu;
  mu = exprm.h_transfer(eta);
  for (int i=0; i<eta.n_rows; ++i) {
      if (!is_finite(eta[i])) {
        Rcpp::Rcout << "warning: NaN or non-finite eta" << std::endl;
        break;
      }
      if (!exprm.valideta(eta[i])) {
        Rcpp::Rcout << "warning: eta is not in the support" << std::endl;
        break;
      }
  }

  //check the validity of mu for Poisson and Binomial family
  double eps = 10. * datum::eps;
  if (exprm.model_name == "poisson")
    if (any(vectorise(mu) < eps))
      Rcpp::Rcout << "warning: sgd.fit: fitted rates numerically 0 occurred" << std::endl;
  if (exprm.model_name == "binomial")
      if (any(vectorise(mu) < eps) or any(vectorise(mu) > (1-eps)))
        Rcpp::Rcout << "warning: sgd.fit: fitted rates numerically 0 occurred" << std::endl;

  //calculate the deviance
  double dev = exprm.deviance(data.Y, mu, exprm.weights);

  //check the number of features
  if (X_rank < Sgd_dataset_size(data).d) {
    for (int i = X_rank; i < coef.n_rows; i++) {
      coef.row(i) = datum::nan;
    }
  }
  return Rcpp::List::create(
    Rcpp::Named("mu") = mu,
    Rcpp::Named("eta") = eta,
    Rcpp::Named("rank") = X_rank,
    Rcpp::Named("deviance") = dev);
}

// TODO
// post_process_ee
// model.out: flag to include weighting matrix

// use the method specified by method to estimate parameters
// [[Rcpp::export]]
Rcpp::List run_online_algorithm(SEXP dataset, SEXP experiment, SEXP method,
  SEXP verbose) {
  Rcpp::List Experiment(experiment);

  std::string model_name = Rcpp::as<std::string>(Experiment["name"]);
  Rcpp::List model_attrs = Experiment["model.attrs"];

  if (model_name == "gaussian" || model_name == "poisson" || model_name == "binomial" || model_name == "gamma") {
    Sgd_Experiment_Glm exprm(model_name, model_attrs);
    return run_experiment(dataset, method, verbose, exprm, Experiment);
  } else if (model_name == "ee") {
    Sgd_Experiment_Ee exprm(model_name, model_attrs);
    return run_experiment(dataset, method, verbose, exprm, Experiment);
  } else {
    return Rcpp::List();
  }
}

template<typename EXPERIMENT>
Rcpp::List run_experiment(SEXP dataset, SEXP method, SEXP verbose, EXPERIMENT
  exprm, Rcpp::List Experiment) {
  Rcpp::List Dataset(dataset);

  Sgd_Dataset data;
  data.X = Rcpp::as<mat>(Dataset["X"]);
  data.Y = Rcpp::as<mat>(Dataset["Y"]);
  std::string meth = Rcpp::as<std::string>(method);

  exprm.n_iters = Rcpp::as<unsigned>(Experiment["niters"]);
  exprm.d = Rcpp::as<unsigned>(Experiment["d"]);
  exprm.start = Rcpp::as<mat>(Experiment["start"]);
  exprm.weights = Rcpp::as<mat>(Experiment["weights"]);
  exprm.offset = Rcpp::as<mat>(Experiment["offset"]);
  exprm.epsilon = Rcpp::as<double>(Experiment["epsilon"]);
  exprm.trace = Rcpp::as<bool>(Experiment["trace"]);
  exprm.dev = Rcpp::as<bool>(Experiment["deviance"]);
  exprm.convergence = Rcpp::as<bool>(Experiment["convergence"]);
  std::string lr = Rcpp::as<std::string>(Experiment["lr"]);
  if (lr == "one-dim") {
    // use the min eigenvalue of the covariance of data as alpha in LR
    // TODO this can be arbitrarily small
    cx_vec eigval;
    cx_mat eigvec;
    // eig_gen(eigval, eigvec, data.covariance());
    // double lr_alpha = min(eigval).real();
    // if (lr_alpha < 1e-8) {
      // lr_alpha = 1; // temp hack
    // }
    double lr_alpha = 1;
    double c;
    if (meth == "asgd" || meth == "ai-sgd") {
      c = 2./3.;
    }
    else {
      c = 1.;
    }
    exprm.init_one_dim_learning_rate(1., lr_alpha, c, 1.);
  }
  else if (lr == "one-dim-eigen") {
    exprm.init_one_dim_eigen_learning_rate();
  }
  else if (lr == "d-dim") {
    exprm.init_ddim_learning_rate(0., 1.);
  }
  else if (lr == "adagrad") {
    exprm.init_ddim_learning_rate(1., .5);
  }

  Sgd_OnlineOutput out(data, exprm.start);
  unsigned nsamples = Sgd_dataset_size(data).nsamples;

  // check if the number of observations is greater than the rank of X
  unsigned X_rank = nsamples;
  if (exprm.model_name == "gaussian" || exprm.model_name == "poisson" || exprm.model_name == "binomial" || exprm.model_name == "gamma") {
    if (exprm.rank) {
      X_rank = rank(data.X);
      if (X_rank > nsamples) {
        Rcpp::Rcout << "X matrix has rank " << X_rank << ", but only "
          << nsamples << " observation" << std::endl;
        return Rcpp::List();
      }
    }

  }

  // print out info
  #if DEBUG
  Rcpp::Rcout << data;
  Rcpp::Rcout << exprm;
  Rcpp::Rcout << "    Method: " << meth << std::endl;
  #endif

  bool good_gradient = true;
  bool good_validity = true;

  mat theta_new;
  mat theta_old = out.last_estimate();
  mat theta_new_ave;
  mat theta_old_ave;
  bool flag_ave;

  if (meth == "asgd" || meth == "ai-sgd") {
    flag_ave = true;
  }

  for (int t = 1; t <= nsamples; ++t) {
    if (meth == "sgd" || meth == "asgd") {
      theta_new = Sgd_sgd_online_algorithm(t, theta_old, data, exprm, good_gradient);

      if (flag_ave) {
        if (t != 1) {
          theta_new_ave = (1. - 1./(double)t) * theta_old_ave
              + 1./((double)t) * theta_new;
        } else {
          theta_new_ave = theta_new;
        }
        out = theta_new_ave;
        theta_old_ave = theta_new_ave;
      }
      else {
        out = theta_new;
      }
      theta_old = theta_new;

      if (!good_gradient) {
        Rcpp::Rcout << "NA or infinite gradient" << std::endl;
        return Rcpp::List();
      }
    }
    else if (meth == "implicit" || meth == "ai-sgd") {
      // Rcpp::Rcout << t << std::endl;

      theta_new = Sgd_implicit_online_algorithm(t, theta_old, data, exprm, good_gradient);

      if (flag_ave) {
        if (t != 1) {
          theta_new_ave = (1. - 1./(double)t) * theta_old_ave
              + 1./((double)t) * theta_new;
        }
        else {
          theta_new_ave = theta_new;
        }
        out = theta_new_ave;
        theta_old_ave = theta_new_ave;
      }
      else {
        out = theta_new;
      }
      theta_old = theta_new;

      if (!is_finite(theta_old)) {
        Rcpp::Rcout << "warning: non-finite coefficients at iteration " << t <<
          std::endl;
      }
    }
    good_validity = validity_check(data, theta_old, t, exprm);
    if (!good_validity) {
      return Rcpp::List();
    }
  }

  mat coef = out.last_estimate();
  Rcpp::List model_out;
  if (exprm.model_name == "gaussian" ||
      exprm.model_name == "poisson" ||
      exprm.model_name == "binomial" ||
      exprm.model_name == "gamma") {
    model_out = post_process_glm(out, data, exprm, coef, X_rank);
  }
  bool converged = true;
  //Rcpp::Rcout << out.estimates;

  return Rcpp::List::create(
      Rcpp::Named("coefficients") = coef,
      Rcpp::Named("converged") = converged,
      Rcpp::Named("estimates") = out.estimates,
      Rcpp::Named("pos") = out.pos,
      Rcpp::Named("model.out") = model_out);
}
