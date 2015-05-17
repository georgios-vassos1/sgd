// This file was generated by Rcpp::compileAttributes
// Generator token: 10BE3573-1514-4C36-9D1C-5A225CD40393

#include <RcppArmadillo.h>
#include <Rcpp.h>

using namespace Rcpp;

// run_online_algorithm
Rcpp::List run_online_algorithm(SEXP dataset, SEXP experiment, SEXP method, SEXP verbose);
RcppExport SEXP sgd_run_online_algorithm(SEXP datasetSEXP, SEXP experimentSEXP, SEXP methodSEXP, SEXP verboseSEXP) {
BEGIN_RCPP
    SEXP __sexp_result;
    {
        Rcpp::RNGScope __rngScope;
        Rcpp::traits::input_parameter< SEXP >::type dataset(datasetSEXP );
        Rcpp::traits::input_parameter< SEXP >::type experiment(experimentSEXP );
        Rcpp::traits::input_parameter< SEXP >::type method(methodSEXP );
        Rcpp::traits::input_parameter< SEXP >::type verbose(verboseSEXP );
        Rcpp::List __result = run_online_algorithm(dataset, experiment, method, verbose);
        PROTECT(__sexp_result = Rcpp::wrap(__result));
    }
    UNPROTECT(1);
    return __sexp_result;
END_RCPP
}
