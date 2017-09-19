// dear emacs, please treat this as -*- C++ -*-

#include <R.h>
#include <Rmath.h>
#include <Rdefines.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#include "pomp_internal.h"

void eval_skeleton_native (double *f, 
			   double *time, double *x, double *p,
			   int nvars, int npars, int ncovars, int ntimes, 
			   int nrepx, int nrepp, int nreps, 
			   int *sidx, int *pidx, int *cidx,
			   lookup_table *covar_table,
			   pomp_skeleton *fun, SEXP args) 
{
  double *xp, *pp;
  double *covars = NULL;
  int j, k;
  set_pomp_userdata(args);
  if (ncovars > 0) covars = (double *) Calloc(ncovars, double);
  for (k = 0; k < ntimes; k++, time++) { // loop over times
    R_CheckUserInterrupt();	// check for user interrupt
    // interpolate the covar functions for the covariates
    table_lookup(covar_table,*time,covars);
    for (j = 0; j < nreps; j++, f += nvars) { // loop over replicates
      xp = &x[nvars*((j%nrepx)+nrepx*k)];
      pp = &p[npars*(j%nrepp)];
      (*fun)(f,xp,pp,sidx,pidx,cidx,ncovars,covars,*time);
    }
  }
  if (ncovars > 0) Free(covars);
  unset_pomp_userdata();
}

void eval_skeleton_R (double *f,
		      double *time, double *x, double *p,
		      SEXP fcall, SEXP rho, SEXP Snames,
		      double *tp, double *xp, double *pp, double *cp,
		      int nvars, int npars, int ntimes,
		      int nrepx, int nrepp, int nreps,
		      lookup_table *covar_table)
{
  int nprotect = 0;
  int first = 1;
  int use_names;
  SEXP ans, nm;
  double *fs;
  int *posn;
  int i, j, k;

  for (k = 0; k < ntimes; k++, time++) { // loop over times
    R_CheckUserInterrupt();	// check for user interrupt
    *tp = *time;		// copy the time
    // interpolate the covar functions for the covariates
    table_lookup(covar_table,*time,cp);
    for (j = 0; j < nreps; j++, f += nvars) { // loop over replicates
      memcpy(xp,&x[nvars*((j%nrepx)+nrepx*k)],nvars*sizeof(double));
      memcpy(pp,&p[npars*(j%nrepp)],npars*sizeof(double));
      if (first) {
	PROTECT(ans = eval(fcall,rho)); nprotect++;
	if (LENGTH(ans)!=nvars)
	  errorcall(R_NilValue,"in 'skeleton': user 'skeleton' returns a vector of %d state variables but %d are expected",LENGTH(ans),nvars);
	// get name information to fix possible alignment problems
	PROTECT(nm = GET_NAMES(ans)); nprotect++;
	use_names = !isNull(nm);
	if (use_names) {
	  posn = INTEGER(PROTECT(matchnames(Snames,nm,"state variables"))); nprotect++;
	} else {
	  posn = 0;
	}
	fs = REAL(AS_NUMERIC(ans));
	first = 0;
      } else {
	fs = REAL(AS_NUMERIC(eval(fcall,rho)));
      }
      if (use_names) 
	for (i = 0; i < nvars; i++) f[posn[i]] = fs[i];
      else
	for (i = 0; i < nvars; i++) f[i] = fs[i];
    }
  }
  UNPROTECT(nprotect);
}

SEXP do_skeleton (SEXP object, SEXP x, SEXP t, SEXP params, SEXP gnsi)
{
  int nprotect = 0;
  int nvars, npars, nrepp, nrepx, nreps, ntimes, ncovars;
  pompfunmode mode = undef;
  int *dim;
  SEXP Snames, Cnames, Pnames;
  SEXP pompfun;
  SEXP fn, args, F;
  lookup_table covariate_table;

  PROTECT(t = AS_NUMERIC(t)); nprotect++;
  ntimes = LENGTH(t);

  PROTECT(x = as_state_array(x)); nprotect++;
  dim = INTEGER(GET_DIM(x));
  nvars = dim[0]; nrepx = dim[1];
  if (ntimes != dim[2])
    errorcall(R_NilValue,"in 'skeleton': length of 't' and 3rd dimension of 'x' do not agree");

  PROTECT(params = as_matrix(params)); nprotect++;
  dim = INTEGER(GET_DIM(params));
  npars = dim[0]; nrepp = dim[1];

  // 2nd dimension of 'x' and 'params' need not agree
  nreps = (nrepp > nrepx) ? nrepp : nrepx;
  if ((nreps % nrepp != 0) || (nreps % nrepx != 0))
    errorcall(R_NilValue,"in 'skeleton': 2nd dimensions of 'x' and 'params' are incompatible");

  PROTECT(Snames = GET_ROWNAMES(GET_DIMNAMES(x))); nprotect++;
  PROTECT(Pnames = GET_ROWNAMES(GET_DIMNAMES(params))); nprotect++;
  PROTECT(Cnames = GET_COLNAMES(GET_DIMNAMES(GET_SLOT(object,install("covar"))))); nprotect++;
    
  // set up the covariate table
  covariate_table = make_covariate_table(object,&ncovars);

  // extract the user-defined function
  PROTECT(pompfun = GET_SLOT(object,install("skeleton"))); nprotect++;
  PROTECT(fn = pomp_fun_handler(pompfun,gnsi,&mode)); nprotect++;

  // extract 'userdata' as pairlist
  PROTECT(args = VectorToPairList(GET_SLOT(object,install("userdata")))); nprotect++;

  // set up the array to hold results
  {
    int dim[3] = {nvars, nreps, ntimes};
    const char *dimnms[3] = {"variable","rep","time"};
    PROTECT(F = makearray(3,dim)); nprotect++; 
    setrownames(F,Snames,3);
    fixdimnames(F,dimnms,3);
  }

  // first do setup
  switch (mode) {

  case Rfun: 			// R skeleton
    {
      SEXP tvec, xvec, pvec, cvec, fcall, rho;

      PROTECT(tvec = NEW_NUMERIC(1)); nprotect++;
      PROTECT(xvec = NEW_NUMERIC(nvars)); nprotect++;
      PROTECT(pvec = NEW_NUMERIC(npars)); nprotect++;
      PROTECT(cvec = NEW_NUMERIC(ncovars)); nprotect++;
      SET_NAMES(xvec,Snames);
      SET_NAMES(pvec,Pnames);
      SET_NAMES(cvec,Cnames);

      // set up the function call
      PROTECT(fcall = LCONS(cvec,args)); nprotect++;
      SET_TAG(fcall,install("covars"));
      PROTECT(fcall = LCONS(pvec,fcall)); nprotect++;
      SET_TAG(fcall,install("params"));
      PROTECT(fcall = LCONS(tvec,fcall)); nprotect++;
      SET_TAG(fcall,install("t"));
      PROTECT(fcall = LCONS(xvec,fcall)); nprotect++;
      SET_TAG(fcall,install("x"));
      PROTECT(fcall = LCONS(fn,fcall)); nprotect++;
      // environment of the user-defined function
      PROTECT(rho = (CLOENV(fn))); nprotect++;
      
      eval_skeleton_R(REAL(F),REAL(t),REAL(x),REAL(params),
		      fcall,rho,Snames,
		      REAL(tvec),REAL(xvec),REAL(pvec),REAL(cvec),
		      nvars,npars,ntimes,nrepx,nrepp,nreps,&covariate_table);
    }

    break;

  case native:			// native skeleton
    {
      int *sidx, *pidx, *cidx;
      pomp_skeleton *ff = NULL;
      // construct state, parameter, covariate, observable indices
      sidx = INTEGER(PROTECT(name_index(Snames,pompfun,"statenames","state variables"))); nprotect++;
      pidx = INTEGER(PROTECT(name_index(Pnames,pompfun,"paramnames","parameters"))); nprotect++;
      cidx = INTEGER(PROTECT(name_index(Cnames,pompfun,"covarnames","covariates"))); nprotect++;
      // extract the address of the user function
      *((void **) (&ff)) = R_ExternalPtrAddr(fn);
      // make userdata 
      eval_skeleton_native(
			   REAL(F),REAL(t),REAL(x),REAL(params),
			   nvars,npars,ncovars,ntimes,nrepx,nrepp,nreps,
			   sidx,pidx,cidx,&covariate_table,ff,args
			   );
    }

    break;

  default:

    errorcall(R_NilValue,"in 'skeleton': unrecognized 'mode'"); // # nocov

    break;

  }

  UNPROTECT(nprotect);
  return F;
}
