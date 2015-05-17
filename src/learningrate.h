#ifndef LEARNINGRATE_H
#define LEARNINGRATE_H

#include "basedef.h"
#include "data.h"

using namespace arma;

struct Sgd_Learn_Rate_Base;
struct Sgd_Onedim_Learn_Rate;
struct Sgd_Onedim_Eigen_Learn_Rate;
struct Sgd_Ddim_Learn_Rate;
struct Sgd_Learn_Rate_Value;

// The return value for learning_rate method
struct Sgd_Learn_Rate_Value
{
  Sgd_Learn_Rate_Value(unsigned t, unsigned d): type(t), dim(d) {
    if (type == 0) { // scalar
      lr_scalar = 1;
    }
    else if (type == 1) { // vector
      lr_vec = ones<vec>(d);
    }
    else { // matrix
      lr_mat = eye<mat>(d, d);
    }
  }

  mat lr_mat;
  vec lr_vec;
  double lr_scalar;
  unsigned type;
  unsigned dim;
};

mat operator*(const Sgd_Learn_Rate_Value& lr, const mat& grad) {
  if (lr.type == 0) {
    return lr.lr_scalar * grad;
  } else if (lr.type == 1) {
    //int m = grad.n_rows;
    ////int n = grad.n_cols;
    //mat out = zeros<mat>(m, 1);
    //for (unsigned i = 0; i < m; ++i) {
    //  //for (unsigned j = 0; j < n; ++j) {
    //    //out.at(i) += lr.lr_vec.at(i) * grad.at(i, 0);
    //  //}
    //  out.at(i, 0) = lr.lr_vec.at(i) * grad.at(i, 0);
    //}
    //return out;
    //return diagmat(lr.lr_vec) * grad;
    return mat(lr.lr_vec) % grad;
  } else {
    return lr.lr_mat * grad;
  }
}

std::ostream& operator<<(std::ostream& os, const Sgd_Learn_Rate_Value& lr) {
  if (lr.type == 0) {
    os << lr.lr_scalar;
  } else if (lr.type == 1) {
    os << lr.lr_vec;
  }
  else {
    os << lr.lr_mat;
  }
  return os;
}

struct Sgd_Learn_Rate_Base
{
#if DEBUG
  virtual ~Sgd_Learn_Rate_Base() {
    Rcpp::Rcout << "Learning rate object released" << std::endl;
  }
#else
  virtual ~Sgd_Learn_Rate_Base() {}
#endif
  virtual const Sgd_Learn_Rate_Value& learning_rate(const mat& theta_old, const
    Sgd_DataPoint& data_pt, double offset, unsigned t, unsigned d) = 0;
};

/* one-dimensional (scalar) learning rate, suggested in Xu's paper
 */
struct Sgd_Onedim_Learn_Rate : public Sgd_Learn_Rate_Base
{
  Sgd_Onedim_Learn_Rate(double g, double a, double c_, double s) :
  gamma(g), alpha(a), c(c_), scale(s), v(0, 1) { }

  virtual const Sgd_Learn_Rate_Value& learning_rate(const mat& theta_old, const
    Sgd_DataPoint& data_pt, double offset, unsigned t, unsigned d) {
    v.lr_scalar = scale * gamma * pow(1 + alpha * gamma * t, -c);
    return v;
  }

private:
  double gamma;
  double alpha;
  double c;
  double scale;
  Sgd_Learn_Rate_Value v;
};

// one-dimensional learning rate to parameterize a diagonal matrix
struct Sgd_Onedim_Eigen_Learn_Rate : public Sgd_Learn_Rate_Base
{
  Sgd_Onedim_Eigen_Learn_Rate(const grad_func_type& gr) : grad_func(gr), v(0, 1) { }

  virtual const Sgd_Learn_Rate_Value& learning_rate(const mat& theta_old, const
    Sgd_DataPoint& data_pt, double offset, unsigned t, unsigned d) {
    mat Gi = grad_func(theta_old, data_pt, offset);
    // tr(Fisher_matrix) = sum of eigenvalues of Fisher_matrix
    //mat fisher_est = diagmat(Gi * Gi.t()); // vectorized version
    //double sum_eigen = trace(fisher_est);
    double sum_eigen = 0;
    for (unsigned i = 0; i < d; ++i) {
      sum_eigen += pow(Gi.at(i, 0), 2);
    }
    // min_eigen <= d / trace(Fisher_matrix)
    double min_eigen_upper = sum_eigen / d;
    v.lr_scalar = 1. / (min_eigen_upper * t);
    return v;
  }

private:
  grad_func_type grad_func;
  Sgd_Learn_Rate_Value v;
};

// d-dimensional learning rate with parameter weight alpha and exponent c
// AdaGrad: alpha=1, c=1/2
// d-dim: alpha=0, c=1
struct Sgd_Ddim_Learn_Rate : public Sgd_Learn_Rate_Base
{
  Sgd_Ddim_Learn_Rate(unsigned d, double a, double c_, const grad_func_type& gr) :
    Idiag(ones<vec>(d)), alpha(a), c(c_), grad_func(gr), v(2, d) { }

  virtual const Sgd_Learn_Rate_Value& learning_rate(const mat& theta_old, const
    Sgd_DataPoint& data_pt, double offset, unsigned t, unsigned d) {
    mat Gi = grad_func(theta_old, data_pt, offset);
    //Idiag = alpha * Idiag + diagvec(Gi * Gi.t()); // vectorized version
    for (unsigned i = 0; i < d; ++i) {
      Idiag.at(i) = alpha * Idiag.at(i) + pow(Gi.at(i, 0), 2);
    }

    for (unsigned i = 0; i < d; ++i) {
      if (std::abs(Idiag.at(i)) > 1e-8) {
        v.lr_mat.at(i, i) = 1. / pow(Idiag.at(i), c);
      }
      else {
        v.lr_mat.at(i, i) = Idiag.at(i);
      }
    }
    return v;
  }

private:
  vec Idiag;
  double alpha;
  double c;
  grad_func_type grad_func;
  Sgd_Learn_Rate_Value v;
};

#endif
