#include "strikeSlip_powerLaw_qd_fd.hpp"

#define FILENAME "strikeSlip_powerLaw_qd_fd.cpp"

using namespace std;


StrikeSlip_PowerLaw_qd_fd::StrikeSlip_PowerLaw_qd_fd(Domain&D)
: _D(&D),_delim(D._delim),
  _outputDir(D._outputDir),_inputDir(D._inputDir),_loadICs(D._loadICs),
  _vL(1e-9),
  _thermalCoupling("no"),_heatEquationType("transient"),
  _hydraulicCoupling("no"),_hydraulicTimeIntType("explicit"),
  _guessSteadyStateICs(0.),
  _cycleCount(0),_maxNumCycles(1e3),_deltaT(1e-3),_deltaT_fd(1e-3),_CFL(0.5),_ay(NULL),_Fhat(NULL),_alphay(NULL),
  _timeIntegrator("RK32"),_timeControlType("PID"),
  _stride1D(1),_stride2D(1),_maxStepCount(1e8),
  _initTime(0),_currTime(0),_maxTime(1e15),
  _minDeltaT(1e-3),_maxDeltaT(1e10),
  _stepCount(0),_atol(1e-8),_initDeltaT(1e-3),_normType("L2_absolute"),
  _integrateTime(0),_writeTime(0),_linSolveTime(0),_factorTime(0),_startTime(MPI_Wtime()),
  _miscTime(0),_timeV1D(NULL),_dtimeV1D(NULL),_timeV2D(NULL),
  _bcRType("remoteLoading"),_bcTType("freeSurface"),_bcLType("symm_fault"),_bcBType("freeSurface"),
  _quadEx(NULL),_quadImex(NULL),
  _fault_qd(NULL),_material(NULL),_he(NULL),_p(NULL),
  _fss_T(0.2),_fss_EffVisc(0.2),_gss_t(1e-10),_maxSSIts_effVisc(50),_maxSSIts_tau(50),_maxSSIts_timesteps(2e4),
  _atolSS_effVisc(1e-3)
{
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::StrikeSlip_PowerLaw_qd_fd()";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  loadSettings(D._file);
  checkInput();
  _he = new HeatEquation(D); // heat equation
  _fault_qd = new Fault_qd(D,D._scatters["body2L"]); // fault
  if (_thermalCoupling.compare("no")!=0 && _stateLaw.compare("flashHeating")==0) {
    Vec T; VecDuplicate(_D->_y,&T);
    _he->getTemp(T);
    _fault_qd->setThermalFields(T,_he->_k,_he->_c);
  }
  // pressure diffusion equation
  if (_hydraulicCoupling.compare("no")!=0) {
    _p = new PressureEq(D);
  }
  if (_hydraulicCoupling.compare("coupled")==0) {
    _fault_qd->setSNEff(_p->_p);
  }

  // initiate momentum balance equation
  parseBCs();
  _material = new PowerLaw(D,*_he,_mat_qd_bcRType,_mat_qd_bcTType,_mat_qd_bcLType,_mat_qd_bcBType);

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
}


StrikeSlip_PowerLaw_qd_fd::~StrikeSlip_PowerLaw_qd_fd()
{
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::~StrikeSlip_PowerLaw_qd_fd()";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  map<string,Vec>::iterator it;
  for (it = _varQSEx.begin(); it!=_varQSEx.end(); it++ ) {
    VecDestroy(&it->second);
  }
  for (it = _varIm.begin(); it!=_varIm.end(); it++ ) {
    VecDestroy(&it->second);
  }

  { // destroy viewers for steady state iteration
    map<string,std::pair<PetscViewer,string> >::iterator it;
    for (it = _viewers.begin(); it!=_viewers.end(); it++ ) {
      PetscViewerDestroy(& (_viewers[it->first].first) );
    }
  }

  PetscViewerDestroy(&_timeV1D);
  PetscViewerDestroy(&_dtimeV1D);
  PetscViewerDestroy(&_timeV2D);


  delete _quadImex;    _quadImex = NULL;
  delete _quadEx;      _quadEx = NULL;
  delete _material;    _material = NULL;
  delete _fault_qd;       _fault_qd = NULL;
  delete _he;          _he = NULL;
  delete _p;           _p = NULL;

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
}

// loads settings from the input text file
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::loadSettings(const char *file)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
    std::string funcName = "HeatEquation::loadSettings()";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  PetscMPIInt rank,size;
  MPI_Comm_size(PETSC_COMM_WORLD,&size);
  MPI_Comm_rank(PETSC_COMM_WORLD,&rank);

  ifstream infile( file );
  string line,var;
  size_t pos = 0;
  while (getline(infile, line))
  {
    istringstream iss(line);
    pos = line.find(_delim); // find position of the delimiter
    var = line.substr(0,pos);

    if (var.compare("thermalCoupling")==0) {
      _thermalCoupling = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("hydraulicCoupling")==0) {
      _hydraulicCoupling = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("stateLaw")==0) {
      _stateLaw = line.substr(pos+_delim.length(),line.npos).c_str();
    }

    else if (var.compare("guessSteadyStateICs")==0) {
      _guessSteadyStateICs = atoi( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }

    // for steady state iteration
    else if (var.compare("fss_T")==0) {
      _fss_T = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }
    else if (var.compare("fss_EffVisc")==0) {
      _fss_EffVisc = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }
    else if (var.compare("gss_t")==0) {
      _gss_t = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }
    else if (var.compare("maxSSIts_effVisc")==0) {
      _maxSSIts_effVisc = atoi( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }
    else if (var.compare("maxSSIts_tau")==0) {
      _maxSSIts_tau = atoi( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }
    else if (var.compare("maxSSIts_timesteps")==0) {
      _maxSSIts_timesteps = (int) atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }
    else if (var.compare("atolSS_effVisc")==0) {
      _atolSS_effVisc = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }

    // time integration properties
    else if (var.compare("timeIntegrator")==0) {
      _timeIntegrator = line.substr(pos+_delim.length(),line.npos);
    }
    else if (var.compare("timeControlType")==0) {
      _timeControlType = line.substr(pos+_delim.length(),line.npos);
    }
    else if (var.compare("stride1D")==0){ _stride1D = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("stride2D")==0){ _stride2D = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxStepCount")==0) { _maxStepCount = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("initTime")==0) { _initTime = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxTime")==0) { _maxTime = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("minDeltaT")==0) { _minDeltaT = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxDeltaT")==0) {_maxDeltaT = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("initDeltaT")==0) { _initDeltaT = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("atol")==0) { _atol = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("timeIntInds")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_timeIntInds);
    }
    else if (var.compare("normType")==0) { _normType = line.substr(pos+_delim.length(),line.npos).c_str(); }

    else if (var.compare("vL")==0) { _vL = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }

    // boundary conditions for momentum balance equation
    else if (var.compare("momBal_bcR_qd")==0) {
      _bcRType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcT_qd")==0) {
      _bcTType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcL_qd")==0) {
      _bcLType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcB_qd")==0) {
      _bcBType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
  }

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// Check that required fields have been set by the input file
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::checkInput()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::checkInput";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  assert(_guessSteadyStateICs == 0 || _guessSteadyStateICs == 1);
  if (_loadICs) { assert(_guessSteadyStateICs == 0); }

  assert(_thermalCoupling.compare("coupled")==0 ||
      _thermalCoupling.compare("uncoupled")==0 ||
      _thermalCoupling.compare("no")==0 );

  assert(_hydraulicCoupling.compare("coupled")==0 ||
      _hydraulicCoupling.compare("uncoupled")==0 ||
      _hydraulicCoupling.compare("no")==0 );

  assert(_timeIntegrator.compare("FEuler")==0 ||
      _timeIntegrator.compare("RK32")==0 ||
      _timeIntegrator.compare("RK43")==0 ||
      _timeIntegrator.compare("RK32_WBE")==0 ||
      _timeIntegrator.compare("RK43_WBE")==0 ||
      _timeIntegrator.compare("WaveEq")==0 );

  assert(_timeControlType.compare("P")==0 ||
         _timeControlType.compare("PI")==0 ||
         _timeControlType.compare("PID")==0 );

  if (_initDeltaT<_minDeltaT || _initDeltaT < 1e-14) {_initDeltaT = _minDeltaT; }
  assert(_maxStepCount >= 0);
  assert(_initTime >= 0);
  assert(_maxTime >= 0 && _maxTime>=_initTime);
  assert(_atol >= 1e-14);
  assert(_minDeltaT >= 1e-14);
  assert(_maxDeltaT >= 1e-14  &&  _maxDeltaT >= _minDeltaT);
  assert(_initDeltaT>0 && _initDeltaT>=_minDeltaT && _initDeltaT<=_maxDeltaT);

  // check boundary condition types for momentum balance equation
  assert(_bcLType.compare("outGoingCharacteristics")==0 ||
    _bcRType.compare("freeSurface")==0 ||
    _bcRType.compare("tau")==0 ||
    _bcRType.compare("remoteLoading")==0 ||
    _bcRType.compare("symm_fault")==0 ||
    _bcRType.compare("rigid_fault")==0 );

  assert(_bcLType.compare("outGoingCharacteristics")==0 ||
    _bcTType.compare("freeSurface")==0 ||
    _bcTType.compare("tau")==0 ||
    _bcTType.compare("remoteLoading")==0 ||
    _bcTType.compare("symm_fault")==0 ||
    _bcTType.compare("rigid_fault")==0 );

  assert(_bcLType.compare("outGoingCharacteristics")==0 ||
    _bcLType.compare("freeSurface")==0 ||
    _bcLType.compare("tau")==0 ||
    _bcLType.compare("remoteLoading")==0 ||
    _bcLType.compare("symm_fault")==0 ||
    _bcLType.compare("rigid_fault")==0 );

  assert(_bcLType.compare("outGoingCharacteristics")==0 ||
    _bcBType.compare("freeSurface")==0 ||
    _bcBType.compare("tau")==0 ||
    _bcBType.compare("remoteLoading")==0 ||
    _bcBType.compare("symm_fault")==0 ||
    _bcBType.compare("rigid_fault")==0 );

  if (_stateLaw.compare("flashHeating")==0) {
    assert(_thermalCoupling.compare("no")!=0);
  }

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// parse boundary conditions
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::parseBCs()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::parseBCs()";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  if (_bcRType.compare("symm_fault")==0 || _bcRType.compare("rigid_fault")==0 || _bcRType.compare("remoteLoading")==0) {
    _mat_qd_bcRType = "Dirichlet";
  }
  else if (_bcRType.compare("freeSurface")==0 || _bcRType.compare("tau")==0 || _bcRType.compare("outGoingCharacteristics")==0) {
    _mat_qd_bcRType = "Neumann";
  }

  if (_bcTType.compare("symm_fault")==0 || _bcTType.compare("rigid_fault")==0 || _bcTType.compare("remoteLoading")==0) {
    _mat_qd_bcTType = "Dirichlet";
  }
  else if (_bcTType.compare("freeSurface")==0 || _bcTType.compare("tau")==0 || _bcTType.compare("outGoingCharacteristics")==0) {
    _mat_qd_bcTType = "Neumann";
  }

  if (_bcLType.compare("symm_fault")==0 || _bcLType.compare("rigid_fault")==0 || _bcLType.compare("remoteLoading")==0) {
    _mat_qd_bcLType = "Dirichlet";
  }
  else if (_bcLType.compare("freeSurface")==0 || _bcLType.compare("tau")==0 || _bcLType.compare("outGoingCharacteristics")==0) {
    _mat_qd_bcLType = "Neumann";
  }

  if (_bcBType.compare("symm_fault")==0 || _bcBType.compare("rigid_fault")==0 || _bcBType.compare("remoteLoading")==0) {
    _mat_qd_bcBType = "Dirichlet";
  }
  else if (_bcBType.compare("freeSurface")==0 || _bcBType.compare("tau")==0 || _bcBType.compare("outGoingCharacteristics")==0) {
    _mat_qd_bcBType = "Neumann";
  }

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// initiate variables to be integrated in time
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::initiateIntegrand()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::initiateIntegrand()";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  //~ Mat A;
  //~ _material->_sbp->getA(A);
  //~ _material->setupKSP(A,_material->_ksp,_material->_pc);

  Vec slip;
  VecDuplicate(_material->_bcL,&slip);
  VecCopy(_material->_bcL,slip);
  VecScale(slip,2.0);
  //~ VecSet(slip,0.);
  if (_loadICs==1) {
    VecCopy(_fault_qd->_slip,slip);
  }
  _varQSEx["slip"] = slip;

  if (_guessSteadyStateICs) { solveSS(); } // doesn't solve for steady state tau

  _material->initiateIntegrand(_initTime,_varQSEx);
  _fault_qd->initiateIntegrand(_initTime,_varQSEx);

  if (_thermalCoupling.compare("no")!=0 ) {
     _he->initiateIntegrand(_initTime,_varQSEx,_varIm);
     _fault_qd->updateTemperature(_he->_T);
  }

  if (_hydraulicCoupling.compare("no")!=0 ) {
     _p->initiateIntegrand(_initTime,_varQSEx,_varIm);
  }

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}



// monitoring function for ode solvers
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::timeMonitor(const PetscScalar time,const PetscScalar deltaT,
      const PetscInt stepCount, int& stopIntegration)
{
  PetscErrorCode ierr = 0;

  _currTime = time;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::timeMonitor";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
double startTime = MPI_Wtime();

  _stepCount = stepCount;
  _dT = time - _currTime;
  _currTime = time;

  // stopping criteria for time integration
  if (_D->_momentumBalanceType.compare("steadyStateIts")==0) {
  //~ if (_stepCount > 5) { stopIntegration = 1; } // basic test
    //~ PetscScalar maxVel; VecMax(dvarEx.find("slip")->second,NULL,&maxVel);
    PetscScalar maxVel; VecMax(_fault_qd->_slipVel,NULL,&maxVel);
    if (maxVel < 1.2e-9 && time > 1e11) { stopIntegration = 1; }
  }

  if (_stride1D>0 && stepCount % _stride1D == 0) {
    ierr = writeStep1D(stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _fault_qd->writeStep(_stepCount,time,_outputDir); CHKERRQ(ierr);
    if (_hydraulicCoupling.compare("no")!=0) { ierr = _p->writeStep(_stepCount,time,_outputDir); CHKERRQ(ierr); }
    if (_thermalCoupling.compare("no")!=0) { ierr =  _he->writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr); }
  }

  if (_stride2D>0 &&  stepCount % _stride2D == 0) {
    ierr = writeStep2D(stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep2D(_stepCount,time,_outputDir);CHKERRQ(ierr);
    if (_thermalCoupling.compare("no")!=0) { ierr =  _he->writeStep2D(_stepCount,time,_outputDir);CHKERRQ(ierr); }
  }

  if (stepCount % 50 == 0) {
    PetscScalar maxTimeStep_tot, maxDeltaT_momBal = 0.0;
    _material->computeMaxTimeStep(maxDeltaT_momBal);
    maxTimeStep_tot = min(_maxDeltaT,0.8*maxDeltaT_momBal);
    if (_timeIntegrator.compare("RK32_WBE")==0 || _timeIntegrator.compare("RK43_WBE")==0) {
        _quadImex->setTimeStepBounds(_minDeltaT,maxTimeStep_tot);CHKERRQ(ierr);
    }
    else {_quadEx->setTimeStepBounds(_minDeltaT,maxTimeStep_tot);CHKERRQ(ierr); }
  }

  #if VERBOSE > 0
    double _currIntegrateTime = MPI_Wtime() - _startIntegrateTime;
  ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %.15e %.15e\n",stepCount,_currTime,_currIntegrateTime);CHKERRQ(ierr);
    //~ ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %.15e\n",stepCount,_currTime);CHKERRQ(ierr);
  #endif
_writeTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_qd_fd::writeStep1D(const PetscInt stepCount, const PetscScalar time,const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::writeStep1D";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  if (_timeV1D==NULL) {
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(outputDir+"med_time1D.txt").c_str(),&_timeV1D);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_timeV1D, "%.15e\n",time);CHKERRQ(ierr);
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(outputDir+"med_dt1D.txt").c_str(),&_dtimeV1D);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_dtimeV1D, "%.15e\n",_dT);CHKERRQ(ierr);
  }
  else {
    ierr = PetscViewerASCIIPrintf(_timeV1D, "%.15e\n",time);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_dtimeV1D, "%.15e\n",_dT);CHKERRQ(ierr);
  }

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_qd_fd::writeStep2D(const PetscInt stepCount, const PetscScalar time,const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::writeStep1D";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  if (_timeV2D==NULL) {
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(outputDir+"med_time2D.txt").c_str(),&_timeV2D);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_timeV2D, "%.15e\n",time);CHKERRQ(ierr);
  }
  else {
    ierr = PetscViewerASCIIPrintf(_timeV2D, "%.15e\n",time);CHKERRQ(ierr);
  }

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}


PetscErrorCode StrikeSlip_PowerLaw_qd_fd::view()
{
  PetscErrorCode ierr = 0;

  double totRunTime = MPI_Wtime() - _startTime;

  if (_timeIntegrator.compare("IMEX")==0&& _quadImex!=NULL) { ierr = _quadImex->view(); }
  if (_timeIntegrator.compare("RK32")==0 && _quadEx!=NULL) { ierr = _quadEx->view(); }

  _material->view(_integrateTime);
  _fault_qd->view(_integrateTime);
  if (_hydraulicCoupling.compare("no")!=0) { _p->view(_integrateTime); }
  if (_thermalCoupling.compare("no")!=0) { _he->view(); }

  ierr = PetscPrintf(PETSC_COMM_WORLD,"-------------------------------\n\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"StrikeSlip_PowerLaw_qd_fd Runtime Summary:\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent in integration (s): %g\n",_integrateTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent writing output (s): %g\n",_writeTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   %% integration time spent writing output: %g\n",_writeTime/totRunTime*100.);CHKERRQ(ierr);
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_qd_fd::writeContext()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::writeContext";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  // output scalar fields
  std::string str = _outputDir + "mediator_context.txt";
  PetscViewer    viewer;
  PetscViewerCreate(PETSC_COMM_WORLD, &viewer);
  PetscViewerSetType(viewer, PETSCVIEWERASCII);
  PetscViewerFileSetMode(viewer, FILE_MODE_WRITE);
  PetscViewerFileSetName(viewer, str.c_str());
  ierr = PetscViewerASCIIPrintf(viewer,"thermalCoupling = %s\n",_thermalCoupling.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"hydraulicCoupling = %s\n",_hydraulicCoupling.c_str());CHKERRQ(ierr);

  ierr = PetscViewerASCIIPrintf(viewer,"vL = %g\n",_vL);CHKERRQ(ierr);

  // time integration settings
  ierr = PetscViewerASCIIPrintf(viewer,"timeIntegrator = %s\n",_timeIntegrator.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"timeControlType = %s\n",_timeControlType.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"stride1D = %i\n",_stride1D);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"stride2D = %i\n",_stride1D);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxStepCount = %i\n",_maxStepCount);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"initTime = %.15e # (s)\n",_initTime);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxTime = %.15e # (s)\n",_maxTime);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"minDeltaT = %.15e # (s)\n",_minDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxDeltaT = %.15e # (s)\n",_maxDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"initDeltaT = %.15e # (s)\n",_initDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"atol = %.15e\n",_atol);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"timeIntInds = %s\n",vector2str(_timeIntInds).c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"\n");CHKERRQ(ierr);

  PetscViewerDestroy(&viewer);

  _material->writeContext(_outputDir);
   _he->writeContext(_outputDir);
  _fault_qd->writeContext(_outputDir);

  if (_hydraulicCoupling.compare("no")!=0) {
    _p->writeContext(_outputDir);
  }

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

//======================================================================
// Adaptive time stepping functions
//======================================================================

PetscErrorCode StrikeSlip_PowerLaw_qd_fd::integrate()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::integrate";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  double startTime = MPI_Wtime();
  _startIntegrateTime = MPI_Wtime();

  initiateIntegrand(); // put initial conditions into var for integration
  _stepCount = 0;

  // initialize time integrator
  if (_timeIntegrator.compare("FEuler")==0) {
    _quadEx = new FEuler(_maxStepCount,_maxTime,_initDeltaT,_timeControlType);
  }
  else if (_timeIntegrator.compare("RK32")==0) {
    _quadEx = new RK32(_maxStepCount,_maxTime,_initDeltaT,_timeControlType);
  }
  else if (_timeIntegrator.compare("RK43")==0) {
    _quadEx = new RK43(_maxStepCount,_maxTime,_initDeltaT,_timeControlType);
  }
  else if (_timeIntegrator.compare("RK32_WBE")==0) {
    _quadImex = new RK32_WBE(_maxStepCount,_maxTime,_initDeltaT,_timeControlType);
  }
  else if (_timeIntegrator.compare("RK43_WBE")==0) {
    _quadImex = new RK43_WBE(_maxStepCount,_maxTime,_initDeltaT,_timeControlType);
  }
  else {
    PetscPrintf(PETSC_COMM_WORLD,"ERROR: timeIntegrator type not understood\n");
    assert(0); // automatically fail
  }

  if (_timeIntegrator.compare("RK32_WBE")==0 || _timeIntegrator.compare("RK43_WBE")==0) {
    _quadImex->setTolerance(_atol);CHKERRQ(ierr);
    _quadImex->setTimeStepBounds(_minDeltaT,_maxDeltaT);CHKERRQ(ierr);
    ierr = _quadImex->setTimeRange(_initTime,_maxTime);
    ierr = _quadImex->setInitialConds(_varQSEx,_varIm);CHKERRQ(ierr);
    ierr = _quadImex->setToleranceType(_normType); CHKERRQ(ierr);
    ierr = _quadImex->setErrInds(_timeIntInds); // control which fields are used to select step size

    ierr = _quadImex->integrate(this);CHKERRQ(ierr);
  }
  else {
    _quadEx->setTolerance(_atol);CHKERRQ(ierr);
    _quadEx->setTimeStepBounds(_minDeltaT,_maxDeltaT);CHKERRQ(ierr);
    ierr = _quadEx->setTimeRange(_initTime,_maxTime);
    ierr = _quadEx->setToleranceType(_normType); CHKERRQ(ierr);
    ierr = _quadEx->setInitialConds(_varQSEx);CHKERRQ(ierr);
    ierr = _quadEx->setErrInds(_timeIntInds); // control which fields are used to select step size

    ierr = _quadEx->integrate(this);CHKERRQ(ierr);
  }

  _integrateTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// purely explicit time stepping
// note that the heat equation never appears here because it is only ever solved implicitly
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::d_dt(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;

  // update fields based on varEx

  // update for momBal; var holds slip, bcL is displacement at y=0+
  if (_bcLType.compare("symm_fault")==0) {
    ierr = VecCopy(varEx.find("slip")->second,_material->_bcL);CHKERRQ(ierr);
    ierr = VecScale(_material->_bcL,0.5);CHKERRQ(ierr);
  }
  else if (_bcLType.compare("rigid_fault")==0) {
    ierr = VecCopy(varEx.find("slip")->second,_material->_bcL);CHKERRQ(ierr);
  }
  ierr = VecSet(_material->_bcR,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_material->_bcR,1.0,_material->_bcRShift);CHKERRQ(ierr);

  _material->updateFields(time,varEx);
  _fault_qd->updateFields(time,varEx);
  if (varEx.find("pressure") != varEx.end() && _hydraulicCoupling.compare("no")!=0) {
    _p->updateFields(time,varEx);
  }

  // compute rates
  ierr = solveMomentumBalance(time,varEx,dvarEx); CHKERRQ(ierr);
  if (varEx.find("pressure") != varEx.end() && _hydraulicCoupling.compare("no")!=0) {
    _p->d_dt(time,varEx,dvarEx);
  }

  // update fields on fault from other classes
  Vec sxy,sxz,sdev;
  ierr = _material->getStresses(sxy,sxz,sdev);
  //~ ierr = _fault_qd->setTauQS(sxy,sxz); CHKERRQ(ierr); // old
  ierr = _fault_qd->setTauQS(sxy); CHKERRQ(ierr); // new

  if (_hydraulicCoupling.compare("coupled")==0) { _fault_qd->setSNEff(_p->_p); }

  // rates for fault
  if (_bcLType.compare("symm_fault")==0 || _bcLType.compare("rigid_fault")==0) {
    ierr = _fault_qd->d_dt(time,varEx,dvarEx); // sets rates for slip and state
  }
  else {
    VecSet(dvarEx["psi"],0.);
    VecSet(dvarEx["slip"],0.);
  }

  return ierr;
}



// implicit/explicit time stepping
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::d_dt(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx,
      map<string,Vec>& varIm,const map<string,Vec>& varImo,const PetscScalar dt)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::d_dt IMEX";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  // update state of each class from integrated variables varEx and varImo

  // update for momBal; var holds slip, bcL is displacement at y=0+
  if (_bcLType.compare("symm_fault")==0) {
    ierr = VecCopy(varEx.find("slip")->second,_material->_bcL);CHKERRQ(ierr);
    ierr = VecScale(_material->_bcL,0.5);CHKERRQ(ierr);
  }
  else if (_bcLType.compare("rigid_fault")==0) {
    ierr = VecCopy(varEx.find("slip")->second,_material->_bcL);CHKERRQ(ierr);
  }
  ierr = VecSet(_material->_bcR,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_material->_bcR,1.0,_material->_bcRShift);CHKERRQ(ierr);

  _material->updateFields(time,varEx);
  _fault_qd->updateFields(time,varEx);

  if ( varImo.find("pressure") != varImo.end() || varEx.find("pressure") != varEx.end()) {
    _p->updateFields(time,varEx,varImo);
  }

  // update temperature in momBal and fault
  if (varImo.find("Temp") != varImo.end() && _thermalCoupling.compare("coupled")==0) {
    _fault_qd->updateTemperature(varImo.find("Temp")->second);
    _material->updateTemperature(varImo.find("Temp")->second);
  }

  // update effective normal stress in fault using pore pressure
  if (_hydraulicCoupling.compare("coupled")==0) {
    _fault_qd->setSNEff(_p->_p);
  }

  // compute rates
  ierr = solveMomentumBalance(time,varEx,dvarEx); CHKERRQ(ierr);
  if ( varImo.find("pressure") != varImo.end() || varEx.find("pressure") != varEx.end()) {
    _p->d_dt(time,varEx,dvarEx,varIm,varImo,dt);
  }

  // update shear stress on fault from momentum balance computation
  Vec sxy,sxz,sdev;
  ierr = _material->getStresses(sxy,sxz,sdev);
  //~ ierr = _fault_qd->setTauQS(sxy,sxz); CHKERRQ(ierr); // old
  ierr = _fault_qd->setTauQS(sxy); CHKERRQ(ierr); // new

  // rates for fault
  if (_bcLType.compare("symm_fault")==0 || _bcLType.compare("rigid_fault")==0) {
    ierr = _fault_qd->d_dt(time,varEx,dvarEx); // sets rates for slip and state
  }
  else {
    VecSet(dvarEx["psi"],0.);
    VecSet(dvarEx["slip"],0.);
  }


  // heat equation
  if (varIm.find("Temp") != varIm.end()) {
    Vec sxy,sxz,sdev;
    _material->getStresses(sxy,sxz,sdev);
    Vec V = dvarEx.find("slip")->second;
    Vec tau = _fault_qd->_tauP;
    Vec gVxy_t = dvarEx.find("gVxy")->second;
    Vec gVxz_t = dvarEx.find("gVxz")->second;
    Vec Told = varImo.find("Temp")->second;
    ierr = _he->be(time,V,tau,sdev,gVxy_t,gVxz_t,varIm["Temp"],Told,dt); CHKERRQ(ierr);
    // arguments: time, slipVel, txy, sigmadev, dgxy, dgxz, T, old T, dt
  }

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// momentum balance equation and constitutive laws portion of d_dt
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::solveMomentumBalance(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;

  // compute source terms to rhs: d/dy(mu*gVxy) + d/dz(mu*gVxz)
  Vec viscSource;
  ierr = VecDuplicate(_material->_gxy,&viscSource);CHKERRQ(ierr);
  ierr = VecSet(viscSource,0.0);CHKERRQ(ierr);
  ierr = _material->computeViscStrainSourceTerms(viscSource,_material->_gxy,_material->_gxz); CHKERRQ(ierr);

  _material->setRHS();
  ierr = VecAXPY(_material->_rhs,1.0,viscSource); CHKERRQ(ierr);
  VecDestroy(&viscSource);

  // solve for displacement
  ierr = _material->computeU(); CHKERRQ(ierr);

  // update stresses, viscosity, and set shear traction on fault
  ierr = _material->computeTotalStrains(); CHKERRQ(ierr);
  ierr = _material->computeStresses(); CHKERRQ(ierr);
  ierr = _material->computeViscosity(_material->_effViscCap); CHKERRQ(ierr);

  // compute viscous strain rates
  Vec gVxy = varEx.find("gVxy")->second;
  Vec gVxz = varEx.find("gVxz")->second;
  ierr = _material->computeViscStrainRates(time,gVxy,gVxz,dvarEx["gVxy"],dvarEx["gVxz"]); CHKERRQ(ierr);

  return ierr;
}


// for solving fixed point iteration problem, with or without the heat equation
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::integrateSS()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::integrateSS";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  double startTime = MPI_Wtime();


  Vec sxy=NULL,sxz=NULL,sdev = NULL;
  std::string baseOutDir = _outputDir;

   // initial guess for (thermo)mechanical problem
  solveSS();
  Vec T; VecDuplicate(_varSS["effVisc"],&T); _varSS["Temp"] = T; _he->getTemp(_varSS["Temp"]);
  if (_thermalCoupling.compare("coupled")==0) {
    _material->getStresses(sxy,sxz,sdev);
    _he->computeSteadyStateTemp(_currTime,_fault_qd->_slipVel,_fault_qd->_tauP,sdev,_varSS["gVxy_t"],_varSS["gVxz_t"],_varSS["Temp"]);
    _material->updateTemperature(_varSS["Temp"]);
  }
  VecCopy(_fault_qd->_tauQSP,_varSS["tau"]);
  ierr = io_initiateWriteAppend(_viewers, "effVisc", _varSS["effVisc"], _outputDir + "SS_effVisc"); CHKERRQ(ierr);
  ierr = io_initiateWriteAppend(_viewers, "Temp", _varSS["Temp"], _outputDir + "SS_Temp"); CHKERRQ(ierr);


  PetscInt Jj = 0;
  _currTime = _initTime;
  Vec T_old; VecDuplicate(_varSS["Temp"],&T_old); VecSet(T_old,0.);
  _material->initiateIntegrand(_initTime,_varQSEx);
  _fault_qd->initiateIntegrand(_initTime,_varQSEx);

  {
    char buff[5]; sprintf(buff,"%04d",Jj); _outputDir = baseOutDir + string(buff) + "_";
    PetscPrintf(PETSC_COMM_WORLD,"baseDir = %s\n\n",_outputDir.c_str());
    writeSS(Jj,baseOutDir);
    Jj++;
  }
  while (Jj < _maxSSIts_tau) {
    PetscPrintf(PETSC_COMM_WORLD,"Jj = %i, _stepCount = %i\n",Jj,_stepCount);

    // create output path with Jj appended on end
    char buff[5]; sprintf(buff,"%04d",Jj); _outputDir = baseOutDir + string(buff) + "_";
    PetscPrintf(PETSC_COMM_WORLD,"baseDir = %s\n\n",_outputDir.c_str());

    _stepCount = 0;
    _currTime = _initTime;

    // integrate to find the approximate steady state shear stress on the fault
    if (_timeIntegrator.compare("RK32")==0) {
      _quadEx = new RK32(_maxSSIts_timesteps,_maxTime,_initDeltaT,_timeControlType);
    }
    else if (_timeIntegrator.compare("RK43")==0) {
      _quadEx = new RK43(_maxSSIts_timesteps,_maxTime,_initDeltaT,_timeControlType);
    }
    else {
      PetscPrintf(PETSC_COMM_WORLD,"ERROR: time integrator time not acceptable for fixed point iteration method.\n");
      assert(0);
    }
    ierr = _quadEx->setTolerance(_atol); CHKERRQ(ierr);
    ierr = _quadEx->setTimeStepBounds(_minDeltaT,_maxDeltaT);CHKERRQ(ierr);
    ierr = _quadEx->setTimeRange(_initTime,_maxTime); CHKERRQ(ierr);
    ierr = _quadEx->setToleranceType(_normType); CHKERRQ(ierr);
    ierr = _quadEx->setInitialConds(_varQSEx);CHKERRQ(ierr);
    ierr = _quadEx->setErrInds(_timeIntInds);
    ierr = _quadEx->integrate(this);CHKERRQ(ierr);
    delete _quadEx; _quadEx = NULL;

    // compute steady state viscous strain rates and stresses
    VecCopy(_fault_qd->_tauP,_varSS["tau"]);
    solveSSViscoelasticProblem(); // iterate to find effective viscosity etc

    // update temperature, with damping: Tnew = (1-f)*Told + f*Tnew
    if (_thermalCoupling.compare("coupled")==0) {
      _material->getStresses(sxy,sxz,sdev);
      VecCopy(_varSS["Temp"],T_old);
      Vec V; VecDuplicate(_varSS["slipVel"],&V); VecSet(V,_D->_vL);
      VecPointwiseMin(_varSS["slipVel"],V,_varSS["slipVel"]);
      _he->computeSteadyStateTemp(_currTime,_varSS["slipVel"],_fault_qd->_tauP,sdev,_varSS["gVxy_t"],_varSS["gVxz_t"],_varSS["Temp"]);
      VecDestroy(&V);
      VecScale(_varSS["Temp"],_fss_T);
      VecAXPY(_varSS["Temp"],1.-_fss_T,T_old);
      _material->updateTemperature(_varSS["Temp"]);
      _fault_qd->updateTemperature(_varSS["Temp"]);
    }

    ierr = _material->updateSSb(_varSS,_initTime); CHKERRQ(ierr);
    setSSBCs();
    ierr = _material->getStresses(sxy,sxz,sdev);
    ierr = _fault_qd->setTauQS(sxy); CHKERRQ(ierr);

    VecCopy(_fault_qd->_tauP,_varSS["tau"]);
    _material->initiateIntegrand(_initTime,_varQSEx);
    writeSS(Jj,baseOutDir);
    Jj++;
  }
  VecDestroy(&T_old);


  _integrateTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}


// estimate steady state shear stress on fault, store in varSS
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::guessTauSS(map<string,Vec>& varSS)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::guessTauSS";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  // compute steady state stress on fault
  Vec tauRS = NULL,tauVisc = NULL,tauSS=NULL;
  _fault_qd->computeTauRS(tauRS,_vL); // rate and state tauSS assuming velocity is vL
  //~ _fault_qd->getTauRS(tauRS,_vL); // old
  _material->getTauVisc(tauVisc,_gss_t); // tau visc from steady state strain rate

  // tauSS = min(tauRS,tauVisc)
  VecDuplicate(tauRS,&tauSS);
  VecPointwiseMin(tauSS,tauRS,tauVisc);
  //~ VecCopy(tauRS,tauSS);

  if (_inputDir.compare("unspecified") != 0) {
    ierr = loadVecFromInputFile(tauSS,_inputDir,"tauSS"); CHKERRQ(ierr);
    ierr = loadVecFromInputFile(_fault_qd->_psi,_inputDir,"psi"); CHKERRQ(ierr);
  }
  ierr = io_initiateWriteAppend(_viewers, "SS_tauSS", tauSS, _outputDir + "SS_tauSS"); CHKERRQ(ierr);

  // first, set up _varSS
  _varSS["tau"] = tauSS;
  _material->initiateVarSS(_varSS);
  //~ _fault_qd->initiateVarSS(_varSS);
  _varSS["slipVel"] = _fault_qd->_slipVel;
  VecCopy(_varSS["tau"],_fault_qd->_tauQSP);
  VecCopy(_varSS["tau"],_fault_qd->_tauP);

  VecDestroy(&tauRS);
  VecDestroy(&tauVisc);

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}


PetscErrorCode StrikeSlip_PowerLaw_qd_fd::solveSS()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::solveSS";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  guessTauSS(_varSS);
  _material->initiateVarSS(_varSS);

  solveSSViscoelasticProblem(); // converge to steady state eta etc
  ierr = _material->updateSSb(_varSS,_initTime); CHKERRQ(ierr); // solve for gVxy, gVxz
  setSSBCs(); // update u, boundary conditions to be positive, consistent with varEx

  Vec sxy,sxz,sdev;
  ierr = _material->getStresses(sxy,sxz,sdev);
  ierr = _fault_qd->setTauQS(sxy); CHKERRQ(ierr); // new

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// converge to steady state: effective viscosity, sxy, sxz, gVxy, gVxz, gVxy_t, gVxz_t, u
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::solveSSViscoelasticProblem()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::solveSSViscoelasticProblem";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  // set up rhs vector
  VecCopy(_varSS["tau"],_material->_bcL);
  VecSet(_material->_bcR,_vL/2.);

  // loop over effective viscosity
  Vec effVisc_old; VecDuplicate(_varSS["effVisc"],&effVisc_old);
  Vec temp; VecDuplicate(_varSS["effVisc"],&temp); VecSet(temp,0.);
  double err = 1e10;
  int Ii = 0;
  while (Ii < _maxSSIts_effVisc && err >= _atolSS_effVisc) {
    VecCopy(_varSS["effVisc"],effVisc_old);
    _material->setSSRHS(_varSS,"Dirichlet","Neumann","Neumann","Neumann");
    _material->updateSSa(_varSS); // compute v, viscous strain rates
    // update effective viscosity: accepted viscosity = (1-f)*(old viscosity) + f*(new viscosity):
    //~ VecScale(_varSS["effVisc"],_fss_EffVisc);
    //~ VecAXPY(_varSS["effVisc"],1.-_fss_EffVisc,effVisc_old);

    // update effective viscosity: log10(accepted viscosity) = (1-f)*log10(old viscosity) + f*log10(new viscosity):
    MyVecLog10AXPBY(temp,1.-_fss_EffVisc,effVisc_old,_fss_EffVisc,_varSS["effVisc"]);
    VecCopy(temp,_varSS["effVisc"]);

    PetscScalar len;
    VecNorm(effVisc_old,NORM_2,&len);
    err = computeNormDiff_L2_scaleL2(effVisc_old,_varSS["effVisc"]);
    PetscPrintf(PETSC_COMM_WORLD,"    effective viscosity loop: %i %e\n",Ii,err);
    Ii++;
  }
  VecDestroy(&effVisc_old);
  VecDestroy(&temp);

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_qd_fd::writeSS(const int Ii, const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::writeSS";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  if (Ii == 0) {
    ierr = io_initiateWriteAppend(_viewers, "slipVel", _varSS["slipVel"], outputDir + "SS_slipVel"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "tau", _varSS["tau"], outputDir + "SS_tau"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "psi", _fault_qd->_psi, outputDir + "SS_psi"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "effVisc", _varSS["effVisc"], outputDir + "SS_effVisc"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "gVxy_t", _varSS["gVxy_t"], outputDir + "SS_gVxy_t"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "gVxz_t", _varSS["gVxz_t"], outputDir + "SS_gVxz_t"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "sxy", _varSS["sxy"], outputDir + "SS_sxy"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "sxz", _varSS["sxz"], outputDir + "SS_sxz"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "gxy", _varSS["gxy"], outputDir + "SS_gxy"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "gxz", _varSS["gxz"], outputDir + "SS_gxz"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "u", _varSS["u"], outputDir + "SS_u"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "v", _varSS["v"], outputDir + "SS_v"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "Temp", _varSS["Temp"], outputDir + "SS_Temp"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "kTz", _he->_kTz, outputDir + "SS_kTz"); CHKERRQ(ierr);
  }
  else {
    ierr = VecView(_varSS["slipVel"],_viewers["slipVel"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["tau"],_viewers["tau"].first); CHKERRQ(ierr);
    ierr = VecView(_fault_qd->_psi,_viewers["psi"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["effVisc"],_viewers["effVisc"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["gVxy_t"],_viewers["gVxy_t"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["gVxz_t"],_viewers["gVxz_t"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["sxy"],_viewers["sxy"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["sxz"],_viewers["sxz"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["gxy"],_viewers["gxy"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["gxz"],_viewers["gxz"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["u"],_viewers["u"].first); CHKERRQ(ierr);
    ierr = VecView(_varSS["v"],_viewers["v"].first); CHKERRQ(ierr);
    if (_thermalCoupling.compare("coupled")==0) {
      ierr = VecView(_varSS["Temp"],_viewers["Temp"].first); CHKERRQ(ierr);
      ierr = VecView(_he->_kTz,_viewers["kTz"].first); CHKERRQ(ierr);
    }
  }

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}


// update the boundary conditions based on new steady state u
PetscErrorCode StrikeSlip_PowerLaw_qd_fd::setSSBCs()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_qd_fd::setSSBCs";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  // adjust u so it has no negative values
  PetscScalar minVal = 0;
  VecMin(_material->_u,NULL,&minVal);
  if (minVal < 0) {
    minVal = abs(minVal);
    Vec temp;
    VecDuplicate(_material->_u,&temp);
    VecSet(temp,minVal);
    VecAXPY(_material->_u,1.,temp);
    VecDestroy(&temp);
  }

  // extract R boundary from u, to set _material->bcR
  VecScatterBegin(_D->_scatters["body2R"], _material->_u, _material->_bcRShift, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd(_D->_scatters["body2R"], _material->_u, _material->_bcRShift, INSERT_VALUES, SCATTER_FORWARD);
  VecCopy(_material->_bcRShift,_material->_bcR);

  // extract L boundary from u to set slip, possibly _material->_bcL
  Vec uL; VecDuplicate(_material->_bcL,&uL);
  VecScatterBegin(_D->_scatters["body2L"], _material->_u, uL, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd(_D->_scatters["body2L"], _material->_u, uL, INSERT_VALUES, SCATTER_FORWARD);

  if (_varQSEx.find("slip") != _varQSEx.end() ) { VecCopy(uL,_varQSEx["slip"]); }
  else {
    Vec slip;
    VecDuplicate(_material->_bcL,&slip);
    VecCopy(uL,slip);
    _varQSEx["slip"] = slip;
  }
  if (_bcLType.compare("symm_fault")==0) {
    VecScale(_varQSEx["slip"],2.);
  }

  VecDestroy(&uL);

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}



