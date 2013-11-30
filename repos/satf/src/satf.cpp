#include <Rcpp.h>

using namespace Rcpp;

#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <sys/timeb.h>
#include <vector>

#include "satf.h"
#include "satf_math.h"

#define GET(TYPE, NAME) Rcpp::clone( Rcpp::as<TYPE>(params[NAME]) )

#define debug_level 10

_dbg_class_set(CCoefConstraints, "CCoefConstraints", 0);
_dbg_class_set(CResponseVariable, "CResponseVariable", 0);
_dbg_class_set(CDesignMatrix, "CDesignMatrix",0);
_dbg_class_set(CSATFData, "CSATFData", 0);


int getMilliCount(){
  timeb tb;
	ftime(&tb);
	int nCount = tb.millitm + (tb.time & 0xfffff) * 1000;
	return nCount;
}

int getMilliSpan(int nTimeStart){
	int nSpan = getMilliCount() - nTimeStart;
	if(nSpan < 0)
		nSpan += 0x100000 * 1000;
	return nSpan;
}


/**********************************
 *   class CResponseVariable  *
 **********************************/

CResponseVariable::CResponseVariable(DoubleVector& predicted_criterion, CharacterVector& dv, 
                                             CharacterVector& cnames, DataFrame& data)
{
  _dbg_function("constructor", 1);

  mPredictedCriterion = Rcpp::clone( predicted_criterion );
  
  if( dv.containsElementNamed("response") ) {
    _dbg((1, "assigning 1"));
      std::string name_response = as< std::string >(dv["response"]);
      mResponseYes = as< std::vector<int> >(data[ name_response ]);
    _dbg((1, "/assigning 1"));
    mRVType = rv_binary;

  } else if( dv.containsElementNamed("dprime") ) {
      std::string name_dprime = as< std::string >(dv["dprime"]);
      mResponseDprime = as< std::vector<double> >(dv[ name_dprime ]);
      mRVType = rv_dprime;

  } else if( dv.containsElementNamed("n.responses") ) {
      std::string name_yes = as< std::string >(dv["n.responses.yes"]);
      std::string name_all = as< std::string >(dv["n.responses"]);
      mnResponsesYes = as< IntegerVector >(data[ name_yes ]);
      mnResponses = as< IntegerVector >(data[ name_all ]);
      mRVType = rv_aggregate;
  }
  
  if(cnames.containsElementNamed("trial.id") ) {
      std::string name_time = as< std::string >(cnames["trial.id"]);
      mTrialId = as< std::vector<int> >(data[name_time]);
  }
}

/**********************************
 *   class CDesignMatrix  *
 **********************************/


CDesignMatrix::CDesignMatrix(List& contrasts, DataFrame& data, 
                                     CharacterVector& cnames):  
                                        mCoefIndexFirst(parameter_invalid, 0),
                                        mCoefIndexLast(parameter_invalid, 0)
{
  _dbg_function("constructor", 1);

  std::string name_signal = as< std::string >(cnames["signal"]);
  IntegerVector signal = as< IntegerVector >( data[name_signal]);

  // determine the dimensions of the design matrix and set the indices
  CharacterVector names_contrast = contrasts.names();
  mnCoefs = 0;
  for(int i=0; i < contrasts.length(); i++) {
    std::string name = as< std::string >(names_contrast[i]);
    Parameter param_contrast = StringToParameter( name ); 
    CharacterVector terms = as< CharacterVector >(contrasts[i]);
    mCoefIndexFirst[param_contrast] = mnCoefs;
    mnCoefs += terms.length() + 1;
    mCoefIndexLast[param_contrast]  = mnCoefs-1;
  }
  mnDatapoints = data.nrows();

  // construct the design matrix
  NumericMatrix cur_dm = NumericMatrix(mnDatapoints, mnCoefs);
  int offset = 0;
  for(int i=0; i < contrasts.length(); i++) {
    std::string name_contrast = as< std::string >(names_contrast[i]);
    CharacterVector terms = contrasts[ name_contrast ];
    cur_dm(_, offset+0) = signal;
    for(int j=0; j < terms.length(); j++) {
      std::string cur_cname = as< std::string >(terms[j]);
      cur_dm(_, offset+j+1) = as< IntegerVector >( data[ cur_cname ] );
    }
    offset += 1 + terms.length();
  }
  mDM = cur_dm;
}

// void CDesignMatrix::StringToParameter(std::string& name) {  
// }


CDesignMatrix::Parameter CDesignMatrix::StringToParameter(std::string& name)
{
  if(name == "asymptote") {
    return parameter_asymptote;

  } else if(name == "invrate") {
    return parameter_invrate;

  } else if(name == "intercept") {
    return parameter_intercept;

  }
  return parameter_invalid;
}

double CDesignMatrix::GetParameter(Parameter parameter, int datapoint_index, DoubleVector& coefs)  
{
    _dbg_function("GetParameter", 1);

    //_dbg((1, "nrow <%d>, ncol <%d>, coefslen: <%d>", mDM.nrow(), mDM.ncol(), coefs.size()));
    double param = 0.0;
    for(int idx = mCoefIndexFirst[parameter]; idx <= mCoefIndexLast[parameter]; idx++)
    {
      //_dbg((1, "row <%d>, col <%d>", datapoint_index, idx));
       double contrast = mDM(datapoint_index, idx);
      _dbg((0, "mDM(%d, %d) = %f", datapoint_index, idx, contrast));
       if(contrast != 0) {
          param += coefs[idx]*contrast;
       }
    }
    return(param);
}

double CDesignMatrix::ComputeDprime(Rcpp::DoubleVector& coefs, int datapoint_index, double time) 
{
  _dbg_function("ComputeDprime", 1);
  double asymptote = GetParameter(parameter_asymptote, datapoint_index, coefs);
  double invrate   = GetParameter(parameter_invrate,  datapoint_index, coefs);
  double intercept = GetParameter(parameter_intercept, datapoint_index, coefs);
  _dbg((10, "time <%.2f>", time));
  _dbg((10, "index first <%d, %d, %d>", mCoefIndexFirst[0], mCoefIndexFirst[1], mCoefIndexFirst[2] ));
  _dbg((10, "index last  <%d, %d, %d>", mCoefIndexLast[0], mCoefIndexLast[1], mCoefIndexLast[2] ));
  _dbg((10, "coef <%.2f, %.2f, %.2f, %.2f>", coefs[0], coefs[1], coefs[2], coefs[3]));
  _dbg((10, "asymptote <%.2f>, invrate <%.2f>, intercept <%.2f>", asymptote, invrate, intercept));
  _dbg((10, "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"));
  _dbg((10, "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"));
  return SATF(time, asymptote, invrate, intercept);
}


/******************************
 *   class CCoefConstraints   *
 ******************************/
CCoefConstraints::CCoefConstraints(Rcpp::NumericMatrix& coef_constraints) {
    _dbg_function("constructor", 1);
  
  _dbg((0, "copying boundaries"));
    mCoefsLower = coef_constraints(_, 0);
    mCoefsUpper = coef_constraints(_, 1);

    List dimnames(NumericMatrix(coef_constraints).attr("dimnames"));
    if (dimnames.size() > 0) {
      RObject names = dimnames[0];
      if (!names.isNULL()) 
      {
        CharacterVector coef_names = CharacterVector(names);
        _dbg((0, "copying <%d> names", coef_names.size() ));
        for(int i=0; i < coef_names.size(); i++) {
          mCoefNames.push_back( as< std::string >(coef_names[i]) );
        }
      }
    }
    
  // FUTURE-TODO: Align the hyperparams by name
  // if( params.containsElementNamed("hyperparams") )
  //   mHyperparams = as<NumericMatrix>(params["hyperparams"]);
}

void CCoefConstraints::AddCoefficient(double lower, double upper, std::string& name) {
    mCoefsLower.push_back(lower);
    mCoefsUpper.push_back(upper);
    mCoefNames.push_back(name);
}

double CCoefConstraints::TransformWithOneBoundary(double x, double lower, bool constrain) {
  if(constrain) // [-Inf, +Inf] -> [lower, +Inf]
    return pow(x, 2)+lower;
  else // [lower, +Inf] -> [-Inf, +Inf]
    return sqrt(x - lower);
}

double CCoefConstraints::TransformWithTwoBoundaries(double x, double lower, double upper, bool constrain) {
  if(constrain) { // [-Inf, +Inf] -> [0, 1] -> [lower, upper]
    double y = exp(x)/(1+exp(x));
    double z = y*(upper-lower)+lower;
    return(z);

  } else // [lower, upper] -> [0, 1] -> [-Inf, +Inf]
  { 
    double y = (x-lower)/(upper-lower);
    double z = log(y/(1-y));
    return(z);
  }
}
double CCoefConstraints::TransformCoef(double raw_coef, int i, bool constrain)
{
  _dbg_function("TransformCoef", 1);

  if(mCoefsLower[i] == R_NegInf && mCoefsUpper[i] == R_PosInf) {
    return raw_coef;     
  } 
  else if(mCoefsUpper[i] == R_PosInf ) {
    return TransformWithOneBoundary(raw_coef, mCoefsLower[i], constrain);

  } else if( mCoefsLower[i] == R_NegInf) {
    return -1*TransformWithOneBoundary(raw_coef, -mCoefsUpper[i], constrain);
    
  } else {
    return TransformWithTwoBoundaries(raw_coef, mCoefsLower[i], mCoefsUpper[i], constrain);
  }
}

Rcpp::DoubleVector CCoefConstraints::Constrain(Rcpp::DoubleVector& raw_coefs, bool use_names) {
  _dbg_function("Constrain", 1);

  DoubleVector coefs;
  int n_fixed = 0;

  for(int i=0; i < mCoefsLower.size(); i++)
  {
    double val;
    if(mCoefsLower[i] == mCoefsUpper[i]) {
      val = mCoefsLower[i];
      n_fixed++;
      
    } else {
      val = TransformCoef(raw_coefs[i-n_fixed], i, true);

    }
    if(use_names)
      coefs.push_back( val, mCoefNames[i] );  
    else
      coefs.push_back( val );  
  }
/*  
  for(int i=0; i < raw_coefs.size(); i++)
    printf("%.3f, ", raw_coefs[i]);
  printf(" / ");

  for(int i=0; i < coefs.size(); i++)
    printf("%.3f, ", coefs[i]);
  printf("\n");
*/  
  return coefs;
}

DoubleVector CCoefConstraints::Unconstrain(Rcpp::DoubleVector& raw_coefs) {
  _dbg_function("Unconstrain", 1);

  DoubleVector coefs;
  int n_fixed = 0;

  for(int i=0; i < raw_coefs.size(); i++) {
    if(mCoefsLower[i] == mCoefsUpper[i])
      continue;

    double val = TransformCoef(raw_coefs[i-n_fixed], i, false);
    coefs.push_back( val );  
  }
  return coefs;
}


double CCoefConstraints::CoefsLL(DoubleVector& coefs)
{
/*  _dbg_function("CoefsLL", 1);

  if( mHyperparams.length() == 0 )
    return 0;
    
  assert( mHyperparams.ncol() == 2 );
  assert( mHyperparams.nrow() == mDM.nrow() );

  double LL = 0;
  if( mHyperparams.nrow()*mHyperparams.ncol() == 0 )
    return 0;
  
  for(size_t i=0; i < mDM.nCoefs(); i++) {
    NumericVector hyperparams = mHyperparams( _, i);
    LL += _dgamma(coefs[i], hyperparams[0], hyperparams[1], true);
  }
  return LL;
  */
  return 0;
}

/******************************
 *   class CSATFData          *
 ******************************/

CSATFData::CSATFData(CharacterVector& dv, List& contrasts,
                     NumericMatrix& coef_constraints, DataFrame& data, 
                     DoubleVector& predicted_criterion, 
                     CharacterVector& cnames):  
                            mDM(contrasts, data, cnames), 
                            mRV( predicted_criterion, dv, cnames, data ),
                            mCoefConstraints(coef_constraints)
{
  _dbg_function("constructor", 1);

    std::string name_time = as< std::string >(cnames["time"]);
    mTime = as< std::vector<double> >(data[name_time]);
    
}

CSATFData::~CSATFData() {
}

DoubleVector CSATFData::ObjectiveFunction_Binary(DoubleVector& coefs, bool by_row)
{
  _dbg_function("ObjectiveFunction_Binary", 1);

  _dbg((0, "rv_type: %d, datapoints: %d", mRV.GetRVType(), mDM.nDatapoints()));
  assert( mRV.GetRVType() == rv_binary);

  double LL, corr_mrsat, last_dprime = 0;
  bool independent = true;
  DoubleVector LLVector;

  // get mrsat-correlation parameter if there is one
  if( coefs.size() > (int) mDM.nCoefs() ) {
    corr_mrsat = coefs[ mDM.nCoefs() ];
  }
  
  // determine log-likelihood for each datapoint
  for(size_t idx = 0; idx < mDM.nDatapoints(); idx++)
  {
    double dprime = mDM.ComputeDprime(coefs, idx, mTime[idx]);
    
    _dbg((0, "idx <%d>, time <%.1f>, crit <%.2f>, dprime <%.2f>, resp. <%d>", 
      idx, mTime[idx], mRV.PredictedCriterion(idx), dprime, mRV.ResponseYes(idx)));

    if(isnan(dprime)) {
      _dbg((-10, "WARNING: dprime is undefined."));
      printf("SATF WARNING: dprime is undefined.\n");
      if(by_row) 
          LLVector.push_back( R_NaN );
      else
          LLVector[0] += R_NaN;
      return LLVector;
    }

    if( mRV.HasTrialId() ) {
      if(idx > 0 && corr_mrsat != 0) {
        independent = mRV.TrialId(idx)!=mRV.TrialId(idx-1);
      }
    } else {
      independent = true;
    }

    double criterion, pNo, cur_LL;
    if(independent)
    {
        criterion = mRV.PredictedCriterion(idx);
        cur_LL = _pnorm(criterion, dprime, 1.0, (mRV.ResponseYes(idx) == 0), true);

    } else 
    {
      double crit_minus_psi = mRV.PredictedCriterion(idx) - dprime;
      double last_crit_minus_psi = mRV.PredictedCriterion(idx-1) - last_dprime;
      pNo = pnorm_conditional(corr_mrsat, crit_minus_psi, last_crit_minus_psi, mRV.ResponseYes(idx-1) == 1 );

      _dbg((0, "corr.mrsat <%.3f>, crit_minus_psi <%.3f>, last_crit_minus_psi <%.3f>", 
                corr_mrsat, crit_minus_psi, last_crit_minus_psi));

      if(pNo < 0 || pNo > 1 || isnan(pNo)) {
        _dbg((-10, "WARNING: p_No = %f", pNo));
        printf("SATF WARNING, approximation returned invalid probability: p_No = %f\n", pNo);
        if(by_row)
            LLVector.push_back( R_NaN );
        else
            LLVector[0] += R_NaN;
        return LLVector;
      }
      
      if(mRV.ResponseYes(idx) == 1) cur_LL = log(1-pNo);
      else                          cur_LL = log(pNo);
    }
    last_dprime = dprime;

    if(by_row) 
        LLVector.push_back( cur_LL );
    else
      LL += cur_LL;
  }
  if(!by_row)
    LLVector.push_back( LL );

  return LLVector;
}

DoubleVector CSATFData::ObjectiveFunction_Aggregate(Rcpp::DoubleVector& coefs, bool by_row)
{
  _dbg_function("ObjectiveFunction_Aggregate", 1);

  assert( mRV.GetRVType() == rv_aggregate);
  
  DoubleVector LLVector;
  if(!by_row)
    LLVector.push_back( 0 );

// ******
//  mDM.ComputeDprime(coefs, 1, 5.0);
//  mDM.ComputeDprime(coefs, 3, 5.0);
//  return LLVector;  
// ******
  
  for(size_t idx=0; idx < mDM.nDatapoints(); idx++)
  {
    double dprime = mDM.ComputeDprime(coefs, idx, mTime[idx]);
    double criterion = mRV.PredictedCriterion(idx);
    double pYes = 1-_pnorm(criterion, dprime);
    _dbg((0, "idx <%d>, criterion <%.3f>, dprime <%.3f>, p_yes <%.3f>", idx, criterion, dprime, pYes));
    
    int n_responses_yes = mRV.NResponsesYes(idx);
    int n_responses = mRV.NResponses(idx);
    double cur_LL = _dbinom(n_responses_yes, n_responses, pYes, true );
    _dbg((0, "\t\t dbinom(%d, %d, %f) = <%.3f> (LL)", mRV.NResponsesYes(idx), mRV.NResponses(idx), pYes, cur_LL));

    //_dbg((0, "idx <%d>, dprime <%.3f>, p_yes <%.3f>, LL <%.3f>", idx, dprime, pYes, cur_LL));
    
    if(by_row)
      LLVector.push_back( cur_LL );
    else
      LLVector[0] += cur_LL;
  }
  _dbg((0, "returning <%f>", LLVector[0]));
  return LLVector;
}

Rcpp::DoubleVector CSATFData::ObjectiveFunction(DoubleVector& raw_coefs, bool by_row)
{
  _dbg_function("ObjectiveFunction", 1);

  assert(coefs.length() == mnCoefs || coefs.length() == mnCoefs+1 );

  // transform coefficients
  DoubleVector coefs = ConstrainCoefs(raw_coefs, false);

  // init the log-likelihood vector with the log-likelihood of the parameters, given the hyperparameters (or 0 if there are none)
  DoubleVector LL(1);
  LL[0] = mCoefConstraints.CoefsLL(coefs);
  
  // compute the log-likelihood of the data
  switch(mRV.GetRVType()) 
  {
      case rv_aggregate:
        if(by_row)
          return ObjectiveFunction_Aggregate(coefs, by_row);
        else
          return LL + ObjectiveFunction_Aggregate(coefs, by_row);
        break;
        
      case rv_binary:
        if(by_row)
          return ObjectiveFunction_Binary(coefs, by_row);
        else
          return LL + ObjectiveFunction_Binary(coefs, by_row);
        break;
        
      case rv_dprime:
        // TODO: Not implemented for now.
        break;
  }
  return(LL);
}

DoubleVector ObjectiveFunctionCriterion(DoubleVector& coefs, DoubleVector& time, 
                                        IntegerVector& response, IntegerVector& trial_id) 
{
  assert(coefs.size() == 4);
  
  DoubleVector LLVector;
  double upper, lower, center, stretch;
  double LL = 0;
  
  if(time.size() != response.size()) {
    LLVector.push_back(R_NaN);
    return LLVector;
  }
  
  upper = as< double >(coefs["upper"]);
  lower = as< double >(coefs["lower"]);
  center = as< double >(coefs["center"]);
  stretch = as< double >(coefs["stretch"]);

  for(int i=0; i < response.size(); i++) {
    double criterion = CriterionF(time[i], upper, lower, center, stretch);
    LL += _pnorm(criterion, 0.0, 1.0, response[i]==0, true);
  }
  
  LLVector.push_back( LL );
  return LLVector;
}


DoubleVector ComputeCriterion(DoubleVector& coefs, DoubleVector& time)
{
  assert(coefs.size() == 4);
  
  DoubleVector criterion;
  double upper, lower, center, stretch;
  
  upper = as< double >(coefs["upper"]);
  lower = as< double >(coefs["lower"]);
  center = as< double >(coefs["center"]);
  stretch = as< double >(coefs["stretch"]);

  for(int i=0; i < time.size(); i++) {
    criterion.push_back( CriterionF(time[i], upper, lower, center, stretch) );
  }
  return criterion;
}
