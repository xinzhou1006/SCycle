#ifndef PRESSURE_HPP_INCLUDED
#define PRESSURE_HPP_INCLUDED

#include <petscksp.h>
#include <cmath>
#include <assert.h>
#include <vector>
#include <cmath>
#include "genFuncs.hpp"
#include "domain.hpp"
#include "fault.hpp"


class PressureEq
{
  private:

    const char       *_file; // input file
    std::string       _delim; // format is: var delim value (without the white space)
    std::string       _outputDir; // directory for output
    const bool        _isMMS; // true if running mms test
    std::string       _hydraulicTimeIntType; // time integration type (explicit vs implicit)

    // domain properties
    const int          _order;
    const PetscInt     _N;  //number of nodes on fault
    const PetscScalar  _L,_h; // length of fault, grid spacing on fault
    Vec                _z; // vector of z-coordinates on fault (allows for variable grid spacing)


    // material properties
    Vec _n_p,_beta_p,_k_p,_eta_p,_rho_f;
    PetscScalar _g; // gravitational acceleration

    // linear system
    std::string          _linSolver;
    KSP                  _ksp;
    PetscScalar          _kspTol;
    SbpOps              *_sbp;
    std::string          _sbpType;
    int                  _linSolveCount;
    Vec                  _bcL,_bcT,_bcB;
    Vec                  _p_t;
    Mat                  _M;


    // input fields
    std::vector<double>   _n_pVals,_n_pDepths,_beta_pVals,_beta_pDepths,_k_pVals,_k_pDepths;
    std::vector<double>   _eta_pVals,_eta_pDepths,_rho_fVals,_rho_fDepths;
    std::vector<double>   _pVals,_pDepths,_dpVals,_dpDepths;

    // run time monitoring
    double       _writeTime,_linSolveTime,_ptTime,_startTime,_miscTime;

    // viewers
    std::map <string,PetscViewer>  _viewers;

    // disable default copy constructor and assignment operator
    PressureEq(const PressureEq & that);
    PressureEq& operator=( const PressureEq& rhs);

    PetscErrorCode setVecFromVectors(Vec&, vector<double>&,vector<double>&);
    PetscErrorCode  setVecFromVectors(Vec& vec, vector<double>& vals,vector<double>& depths,
      const PetscScalar maxVal);

    PetscErrorCode computeVariableCoefficient(const Vec& p,Vec& coeff);
    PetscErrorCode setUpSBP();
    PetscErrorCode computeInitialSteadyStatePressure(Domain& D);
    PetscErrorCode setUpBe(Domain& D);
    PetscErrorCode setupKSP(const Mat& A);

  public:

    Vec _p; // pressure

    PressureEq(Domain& D);
    ~PressureEq();

    PetscErrorCode setFields(Domain& D);
    PetscErrorCode loadSettings(const char *file);
    PetscErrorCode checkInput();

    PetscErrorCode initiateIntegrand(const PetscScalar time,map<string,Vec>& varEx,map<string,Vec>& varIm);
    PetscErrorCode updateFields(const PetscScalar time,const map<string,Vec>& varEx);
    PetscErrorCode updateFields(const PetscScalar time,const map<string,Vec>& varEx,const map<string,Vec>& varIm);

    // explicit time integration
    PetscErrorCode d_dt(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx);
    PetscErrorCode d_dt_main(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx);
    PetscErrorCode d_dt_mms(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx);

    // implicit time integration
    PetscErrorCode d_dt(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx,
      map<string,Vec>& varIm,const map<string,Vec>& varImo,const PetscScalar dt);
    PetscErrorCode be(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx,
      map<string,Vec>& varIm,const map<string,Vec>& varImo,const PetscScalar dt);
    PetscErrorCode be_mms(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx,
      map<string,Vec>& varIm,const map<string,Vec>& varImo,const PetscScalar dt);


    // IO
    PetscErrorCode view(const double totRunTime);
    PetscErrorCode writeContext();
    PetscErrorCode writeStep(const PetscInt stepCount, const PetscScalar time);
    //~ PetscErrorCode view();

    // mms error
    PetscErrorCode measureMMSError(const double totRunTime);

    static double zzmms_pSource1D(const double z, const double t);
    static double zzmms_pA1D(const double y,const double t);
    static double zzmms_pt1D(const double z,const double t);

};



#endif