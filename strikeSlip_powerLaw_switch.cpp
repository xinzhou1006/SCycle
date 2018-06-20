#include "strikeSlip_powerLaw_switch.hpp"

#define FILENAME "strikeSlip_powerLaw_switch.cpp"

using namespace std;


StrikeSlip_PowerLaw_switch::StrikeSlip_PowerLaw_switch(Domain&D)
: _D(&D),_delim(D._delim),_isMMS(D._isMMS),
  _outputDir(D._outputDir),_inputDir(D._inputDir),_loadICs(D._loadICs),
  _vL(1e-9),
  _thermalCoupling("no"),_heatEquationType("transient"),
  _hydraulicCoupling("no"),_hydraulicTimeIntType("explicit"),
  _guessSteadyStateICs(0.),
  _order(D._order),_Ny(D._Ny),_Nz(D._Nz),
  _Ly(D._Ly),_Lz(D._Lz),
  _deltaT(1e-3), _CFL(0),
  _y(&D._y),_z(&D._z),
  _Fhat(NULL),_savedU(NULL),
  _alphay(D._alphay), _alphaz(D._alphaz),
  _timeIntegrator("RK43"),_timeControlType("PID"),
  _stride1D(1),_stride2D(1),
  _stride1D_qd(1),_stride2D_qd(1),_stride1D_dyn(1),_stride2D_dyn(1),_stride1D_dyn_long(1),_stride2D_dyn_long(1),
  _withFhat(1),
  _maxStepCount_dyn(2000),_maxStepCount_qd(1e8),_maxStepCount(1e6),
  _initTime(0),_currTime(0),_maxTime_dyn(1e15),_maxTime_qd(1e15),_minDeltaT(1e-3),_maxDeltaT(1e10),_maxTime(1e15),
  _inDynamic(false),_firstCycle(true),
  _stepCount(0),_atol(1e-8),_initDeltaT(1e-3),_normType("L2_absolute"),
  _debug(0),_localStep(0),_startOnDynamic(0),
  _timeV1D(NULL),_dtimeV1D(NULL),_timeV2D(NULL),_whichRegime(NULL),
  _integrateTime(0),_writeTime(0),_linSolveTime(0),_factorTime(0),_startTime(MPI_Wtime()),_miscTime(0),_dynTime(0), _qdTime(0),
  _allowed(false), _triggerqd2d(1e-3), _triggerd2qd(1e-3), _limit_qd(1e-8), _limit_dyn(1),_limit_stride_dyn(-1),
  _qd_bcRType("remoteLoading"),_qd_bcTType("freeSurface"),_qd_bcLType("symm_fault"),_qd_bcBType("freeSurface"),
  _dyn_bcRType("outGoingCharacteristics"),_dyn_bcTType("freeSurface"),_dyn_bcLType("outGoingCharacteristics"),_dyn_bcBType("outGoingCharacteristics"),
  _quadEx_qd(NULL),_quadImex_qd(NULL), _quadWaveEx(NULL),
  _fault_qd(NULL),_fault_dyn(NULL), _material(NULL),_he(NULL),_p(NULL),
  _fss_T(0.2),_fss_EffVisc(0.2),_gss_t(1e-10),_maxSSIts_effVisc(50),_maxSSIts_tau(50),_maxSSIts_timesteps(2e4),
  _atolSS_effVisc(1e-3)
{
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::StrikeSlip_PowerLaw_switch()";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  loadSettings(D._file);
  checkInput();
  // if (_thermalCoupling.compare("no")!=0) { // heat equation
    _he = new HeatEquation(D);
  // }
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
  if (_qd_bcRType.compare("symm_fault")==0 || _qd_bcRType.compare("rigid_fault")==0 || _qd_bcRType.compare("remoteLoading")==0) {
    _mat_qd_bcRType = "Dirichlet";
  }
  else if (_qd_bcRType.compare("freeSurface")==0 || _qd_bcRType.compare("tau")==0 || _qd_bcRType.compare("outGoingCharacteristics")==0) {
    _mat_qd_bcRType = "Neumann";
  }

  if (_qd_bcTType.compare("symm_fault")==0 || _qd_bcTType.compare("rigid_fault")==0 || _qd_bcTType.compare("remoteLoading")==0) {
    _mat_qd_bcTType = "Dirichlet";
  }
  else if (_qd_bcTType.compare("freeSurface")==0 || _qd_bcTType.compare("tau")==0 || _qd_bcTType.compare("outGoingCharacteristics")==0) {
    _mat_qd_bcTType = "Neumann";
  }

  if (_qd_bcLType.compare("symm_fault")==0 || _qd_bcLType.compare("rigid_fault")==0 || _qd_bcLType.compare("remoteLoading")==0) {
    _mat_qd_bcLType = "Dirichlet";
  }
  else if (_qd_bcLType.compare("freeSurface")==0 || _qd_bcLType.compare("tau")==0 || _qd_bcLType.compare("outGoingCharacteristics")==0) {
    _mat_qd_bcLType = "Neumann";
  }

  if (_qd_bcBType.compare("symm_fault")==0 || _qd_bcBType.compare("rigid_fault")==0 || _qd_bcBType.compare("remoteLoading")==0) {
    _mat_qd_bcBType = "Dirichlet";
  }
  else if (_qd_bcBType.compare("freeSurface")==0 || _qd_bcBType.compare("tau")==0 || _qd_bcBType.compare("outGoingCharacteristics")==0) {
    _mat_qd_bcBType = "Neumann";
  }

  assert(_qd_bcLType.compare("outGoingCharacteristics")==0 ||
    _qd_bcRType.compare("freeSurface")==0 ||
    _qd_bcRType.compare("tau")==0 ||
    _qd_bcRType.compare("remoteLoading")==0 ||
    _qd_bcRType.compare("symm_fault")==0 ||
    _qd_bcRType.compare("rigid_fault")==0 );

  assert(_qd_bcLType.compare("outGoingCharacteristics")==0 ||
    _qd_bcTType.compare("freeSurface")==0 ||
    _qd_bcTType.compare("tau")==0 ||
    _qd_bcTType.compare("remoteLoading")==0 ||
    _qd_bcTType.compare("symm_fault")==0 ||
    _qd_bcTType.compare("rigid_fault")==0 );

  assert(_qd_bcLType.compare("outGoingCharacteristics")==0 ||
    _qd_bcLType.compare("freeSurface")==0 ||
    _qd_bcLType.compare("tau")==0 ||
    _qd_bcLType.compare("remoteLoading")==0 ||
    _qd_bcLType.compare("symm_fault")==0 ||
    _qd_bcLType.compare("rigid_fault")==0 );

  assert(_qd_bcLType.compare("outGoingCharacteristics")==0 ||
    _qd_bcBType.compare("freeSurface")==0 ||
    _qd_bcBType.compare("tau")==0 ||
    _qd_bcBType.compare("remoteLoading")==0 ||
    _qd_bcBType.compare("symm_fault")==0 ||
    _qd_bcBType.compare("rigid_fault")==0 );

  _mat_dyn_bcBType = "Neumann";
  _mat_dyn_bcTType = "Neumann";
  _mat_dyn_bcRType = "Neumann";
  _mat_dyn_bcLType = "Neumann";

  if (_guessSteadyStateICs) { _material = new PowerLaw(D,*_he,_mat_qd_bcRType,_mat_qd_bcTType,"Neumann",_mat_qd_bcBType); }
  else {_material = new PowerLaw(D,*_he,_mat_qd_bcRType,_mat_qd_bcTType,_mat_qd_bcLType,_mat_qd_bcBType); }

  _cs = *(&(_material->_cs));
  _rhoVec = *(&(_material->_rhoVec));
  _muVec = *(&(_material->_muVec));

  if(_D->_sbpType.compare("mfc_coordTrans")==0){
    Mat J,Jinv,qy,rz,yq,zr;
    _material->_sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr);
    Vec temp1, temp2;
    VecDuplicate(_alphay, &temp1);
    VecDuplicate(_alphay, &temp2);
    MatMult(yq, _alphay, temp1);
    MatMult(zr, _alphaz, temp2);
    VecCopy(temp1, _alphay);
    VecCopy(temp2, _alphaz);
    VecCopy(temp1, D._alphay);
    VecCopy(temp2, D._alphaz);
    VecDestroy(&temp1);
    VecDestroy(&temp2);
  }

  _fault_dyn = new Fault_dyn(D, D._scatters["body2L"]); // fault

  // Change CFL deltaT
  if (_CFL !=0){
    PetscInt max_index;
    PetscScalar max_speed;
    VecMax(_cs,&max_index,&max_speed);
    // Change for variable grid spacing with min y_q 1 / (Ny - 1)
    if (_D->_sbpType.compare("mfc_coordTrans")==0){
      Mat J,Jinv,qy,rz,yq,zr;
      _material->_sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr);

      PetscInt index_z, index_y;
      PetscScalar min_z, min_y;
      Vec yy, zz;
      VecDuplicate(*_y, &yy);
      VecDuplicate(*_z, &zz);
      MatGetDiagonal(yq, yy);
      MatGetDiagonal(zr, zz);
      VecMin(yy,&index_y,&min_y);
      VecMin(zz,&index_z,&min_z);

      _deltaT = 0.5 * _CFL / max_speed * min(min_y / (_Ny - 1), min_z / (_Nz - 1));
    }
    else{
      _deltaT = 0.5 * _CFL / max_speed * min(_Ly / (_Ny - 1), _Lz / (_Nz - 1));
    }
  }
  else{
    PetscInt max_index;
    PetscScalar max_speed, theoretical_dT;
    VecMax(_cs,&max_index,&max_speed);
    if (_D->_sbpType.compare("mfc_coordTrans")==0){
      Mat J,Jinv,qy,rz,yq,zr;
      _material->_sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr);

      PetscInt index_z, index_y;
      PetscScalar min_z, min_y;
      Vec yy, zz;
      VecDuplicate(*_y, &yy);
      VecDuplicate(*_z, &zz);
      MatGetDiagonal(yq, yy);
      MatGetDiagonal(zr, zz);
      VecMin(yy,&index_y,&min_y);
      VecMin(zz,&index_z,&min_z);

      PetscScalar cfl = 0.5;
      theoretical_dT = 0.5 * cfl / max_speed * min(min_y / (_Ny - 1), min_z / (_Nz - 1));
    }
    else{
      PetscScalar cfl = 0.5;
      theoretical_dT = 0.5 * cfl / max_speed * min(_Ly / (_Ny - 1), _Lz / (_Nz - 1));
    }
    if (theoretical_dT > _deltaT){
      PetscPrintf(PETSC_COMM_WORLD, "WARNING : The specified deltaT odes not meet the CFL requirements...");
    }
  }

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
}


StrikeSlip_PowerLaw_switch::~StrikeSlip_PowerLaw_switch()
{
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::~StrikeSlip_PowerLaw_switch()";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

{
  map<string,Vec>::iterator it;
  for (it = _varEx.begin(); it!=_varEx.end(); it++ ) {
    VecDestroy(&it->second);
  }
  for (it = _varIm.begin(); it!=_varIm.end(); it++ ) {
    VecDestroy(&it->second);
  }
}

  //~ for (std::map <string,std::pair<PetscViewer,string> > it = _viewers.begin(); it!=_viewers.end(); it++ ) {
    //~ PetscViewerDestroy(&it->second);
  //~ }

  PetscViewerDestroy(&_timeV1D);
  PetscViewerDestroy(&_dtimeV1D);
  PetscViewerDestroy(&_timeV2D);
  PetscViewerDestroy(&_whichRegime);

  delete _quadImex_qd;    _quadImex_qd = NULL;
  delete _quadEx_qd;      _quadEx_qd = NULL;
  delete _material;    _material = NULL;
  delete _fault_qd;       _fault_qd = NULL;
  delete _fault_dyn;       _fault_dyn = NULL;
  delete _he;          _he = NULL;
  delete _p;           _p = NULL;

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
}

// loads settings from the input text file
PetscErrorCode StrikeSlip_PowerLaw_switch::loadSettings(const char *file)
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

    // time integration properties
    else if (var.compare("timeIntegrator")==0) {
      _timeIntegrator = line.substr(pos+_delim.length(),line.npos);
    }
    else if (var.compare("timeControlType")==0) {
      _timeControlType = line.substr(pos+_delim.length(),line.npos);
    }
    else if (var.compare("stride1D")==0){ _stride1D = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("stride2D")==0){ _stride2D = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("stride1D_qd")==0){ _stride1D_qd = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("stride2D_qd")==0){ _stride2D_qd = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("stride1D_dyn")==0){ _stride1D_dyn = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("stride2D_dyn")==0){ _stride2D_dyn = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("stride1D_dyn_long")==0){ _stride1D_dyn_long = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("stride2D_dyn_long")==0){ _stride2D_dyn_long = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("withFhat")==0){ _withFhat = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxStepCount_dyn")==0) { _maxStepCount_dyn = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxStepCount_qd")==0) { _maxStepCount_qd = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxStepCount")==0) { _maxStepCount = (int)atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("initTime")==0) { _initTime = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxTime_qd")==0) { _maxTime_qd = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxTime_dyn")==0) { _maxTime_dyn = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxTime")==0) { _maxTime = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("minDeltaT")==0) { _minDeltaT = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("maxDeltaT")==0) {_maxDeltaT = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("initDeltaT")==0) { _initDeltaT = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("atol")==0) { _atol = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("timeIntInds")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_timeIntInds);
    }

    else if (var.compare("vL")==0) { _vL = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }

    // boundary conditions for momentum balance equation
    else if (var.compare("momBal_bcR_dyn")==0) {
      _dyn_bcRType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcT_dyn")==0) {
      _dyn_bcTType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcL_dyn")==0) {
      _dyn_bcLType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcB_dyn")==0) {
      _dyn_bcBType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcR_qd")==0) {
      _qd_bcRType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcT_qd")==0) {
      _qd_bcTType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcL_qd")==0) {
      _qd_bcLType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("momBal_bcB_qd")==0) {
      _qd_bcBType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("triggerqd2d")==0) { _triggerqd2d = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("triggerd2qd")==0) { _triggerd2qd = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }

    else if (var.compare("deltaT")==0) { _deltaT = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("CFL")==0) { _CFL = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("limit_qd")==0) { _limit_qd = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("limit_dyn")==0) { _limit_dyn = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("limit_stride_dyn")==0) { _limit_stride_dyn = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("isFault")==0) { _isFault = line.substr(pos+_delim.length(),line.npos).c_str(); }
    else if (var.compare("initialConditions")==0) { _initialConditions = line.substr(pos+_delim.length(),line.npos).c_str(); }
    else if (var.compare("inputDir")==0) { _inputDir = line.substr(pos+_delim.length(),line.npos).c_str(); }

    else if (var.compare("debug")==0) { _debug = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }
    else if (var.compare("startOnDynamic")==0) { _startOnDynamic = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }

  }
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}
// Check that required fields have been set by the input file
PetscErrorCode StrikeSlip_PowerLaw_switch::checkInput()
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

  assert(_timeIntegrator.compare("FEuler")==0
    || _timeIntegrator.compare("RK32")==0
    || _timeIntegrator.compare("RK43")==0
    || _timeIntegrator.compare("RK32_WBE")==0
    || _timeIntegrator.compare("RK43_WBE")==0
    || _timeIntegrator.compare("WaveEq")==0);

  assert(_timeControlType.compare("P")==0 ||
         _timeControlType.compare("PI")==0 ||
         _timeControlType.compare("PID")==0 );

  if (_initDeltaT<_minDeltaT || _initDeltaT < 1e-14) {_initDeltaT = _minDeltaT; }
  assert(_maxStepCount_dyn >= 0);
  assert(_initTime >= 0);
  assert(_maxTime_dyn >= 0 && _maxTime_dyn>=_initTime);
  assert(_atol >= 1e-14);
  assert(_minDeltaT >= 1e-14);
  assert(_maxDeltaT >= 1e-14  &&  _maxDeltaT >= _minDeltaT);
  assert(_initDeltaT>0 && _initDeltaT>=_minDeltaT && _initDeltaT<=_maxDeltaT);

    // check boundary condition types for momentum balance equation
  assert(_qd_bcLType.compare("outGoingCharacteristics")==0 ||
    _qd_bcRType.compare("freeSurface")==0 ||
    _qd_bcRType.compare("tau")==0 ||
    _qd_bcRType.compare("remoteLoading")==0 ||
    _qd_bcRType.compare("symm_fault")==0 ||
    _qd_bcRType.compare("rigid_fault")==0 );

  assert(_qd_bcLType.compare("outGoingCharacteristics")==0 ||
    _qd_bcTType.compare("freeSurface")==0 ||
    _qd_bcTType.compare("tau")==0 ||
    _qd_bcTType.compare("remoteLoading")==0 ||
    _qd_bcTType.compare("symm_fault")==0 ||
    _qd_bcTType.compare("rigid_fault")==0 );

  assert(_qd_bcLType.compare("outGoingCharacteristics")==0 ||
    _qd_bcLType.compare("freeSurface")==0 ||
    _qd_bcLType.compare("tau")==0 ||
    _qd_bcLType.compare("remoteLoading")==0 ||
    _qd_bcLType.compare("symm_fault")==0 ||
    _qd_bcLType.compare("rigid_fault")==0 );

  assert(_qd_bcLType.compare("outGoingCharacteristics")==0 ||
    _qd_bcBType.compare("freeSurface")==0 ||
    _qd_bcBType.compare("tau")==0 ||
    _qd_bcBType.compare("remoteLoading")==0 ||
    _qd_bcBType.compare("symm_fault")==0 ||
    _qd_bcBType.compare("rigid_fault")==0 );

  if (_stateLaw.compare("flashHeating")==0) {
    assert(_thermalCoupling.compare("no")!=0);
  }

  if (_limit_dyn < _triggerqd2d){
    _limit_dyn = 10 * _triggerqd2d;
  }

  if (_limit_qd > _triggerd2qd){
    _limit_qd = _triggerqd2d / 10.0;
  }

  if (_limit_stride_dyn == -1){
    _limit_stride_dyn = _limit_dyn / 10.0;
  }

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::integrate(){
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::integrate";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  double startTime0 = MPI_Wtime();

  integrate_qd();
  _qdTime += MPI_Wtime() - startTime0;
  double startTime1 = MPI_Wtime();
  integrate_dyn();
  _dynTime += MPI_Wtime() - startTime1;
  _integrateTime += MPI_Wtime() - startTime0;
  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::d_dt(const PetscScalar time,map<string,Vec>& varEx,map<string,Vec>& dvarEx){
  PetscErrorCode ierr = 0;
  if(_inDynamic){
    ierr = StrikeSlip_PowerLaw_switch::d_dt_dyn(time,varEx,dvarEx);
  }
  else{
    ierr = StrikeSlip_PowerLaw_switch::d_dt_qd(time,varEx,dvarEx);
  }
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::d_dt(const PetscScalar time, const map<string,Vec>& varEx,map<string,Vec>& dvarEx){
  PetscErrorCode ierr = 0;
  if(!_inDynamic){
    ierr = StrikeSlip_PowerLaw_switch::d_dt_qd(time,varEx,dvarEx);
  }
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::d_dt(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx,
      map<string,Vec>& varIm,const map<string,Vec>& varImo,const PetscScalar dt){
  PetscErrorCode ierr = 0;
  ierr = StrikeSlip_PowerLaw_switch::d_dt_qd(time,varEx,dvarEx,varIm,varImo,dt);
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::d_dt(const PetscScalar time,map<string,Vec>& varEx,map<string,Vec>& dvarEx,
      map<string,Vec>& varIm,map<string,Vec>& varImo){
  PetscErrorCode ierr = 0;
  ierr = StrikeSlip_PowerLaw_switch::d_dt_dyn(time,varEx,dvarEx,varIm,varImo);
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::timeMonitor(const PetscScalar time,const PetscInt stepCount,
      const map<string,Vec>& varEx,const map<string,Vec>& dvarEx,int& stopIntegration){
  PetscErrorCode ierr = 0;
  if(_inDynamic){
    ierr = StrikeSlip_PowerLaw_switch::timeMonitor_dyn(time,stepCount,varEx,dvarEx,stopIntegration);
  }
  else{
    ierr = StrikeSlip_PowerLaw_switch::timeMonitor_qd(time,stepCount,varEx,dvarEx,stopIntegration);
  }
  _localStep += 1;
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::timeMonitor(const PetscScalar time,const PetscInt stepCount,
      const map<string,Vec>& varEx,const map<string,Vec>& dvarEx,const map<string,Vec>& varIm,int& stopIntegration){
  PetscErrorCode ierr = 0;
  if(_inDynamic){
    ierr = StrikeSlip_PowerLaw_switch::timeMonitor_dyn(time,stepCount,varEx,dvarEx,varIm,stopIntegration);
  }
  else{
    ierr = StrikeSlip_PowerLaw_switch::timeMonitor_qd(time,stepCount,varEx,dvarEx,varIm,stopIntegration);
  }
  _localStep += 1;
  return ierr;
  }

bool StrikeSlip_PowerLaw_switch::check_switch(const Fault* _fault){
  bool mustswitch = false;
  Vec absSlipVel;
  VecDuplicate(_fault->_slipVel, &absSlipVel);
  VecCopy(_fault->_slipVel, absSlipVel);
  PetscInt index;
  PetscScalar max_value;
  VecAbs(absSlipVel);
  VecMax(absSlipVel, &index, &max_value);

  #if VERBOSE > 0
    PetscPrintf(PETSC_COMM_WORLD, "maxslipVel = %g\n", max_value);
  #endif

  if(_currTime > _maxTime || _stepCount > _maxStepCount){
    mustswitch = true;
    _maxStepCount_dyn = 0;
    _maxStepCount_qd = 0;
    _D->_numCycles = 0;
  }
  if(_inDynamic){
    if(!_allowed){
      if(max_value > _limit_dyn){
        _allowed = true;
      }
    }
    if (_allowed && max_value < _limit_stride_dyn){
      _stride1D = _stride1D_dyn_long;
      _stride2D = _stride2D_dyn_long;
    }
    if(_allowed && max_value < _triggerd2qd){
      mustswitch = true;
    }
  }
  else{
    if(!_allowed){
      if(max_value < _limit_qd){
        _allowed = true;
      }
    }
    if(_allowed && max_value > _triggerqd2d){
      mustswitch = true;
    }
  }
  VecDestroy(&absSlipVel);
  return mustswitch;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::reset_for_qd(){
  PetscErrorCode ierr = 0;
  // Force writing output
  if(_stepCount % _stride1D > 0){
    PetscInt stride1d, stride2d;
    stride1d = _stride1D;
    stride2d = _stride2D;
    _stride1D = 1;
    _stride2D = 1;
    timeMonitor(_currTime, _stepCount, _varEx, _varEx, _stride1D);
    _stride1D = stride1d;
    _stride2D = stride2d;
  }

  _localStep = 0;

  _allowed = false;
  // _limit = 1e-8;
  _varEx = _quadWaveEx->getVar();
  _firstCycle = false;
  _inDynamic = false;

  VecCopy(_fault_dyn->_psi, _varEx["psi"]);
  VecCopy(_fault_dyn->_psi, _fault_qd->_psi);
  VecCopy(_fault_dyn->_slipVel, _fault_qd->_slipVel);

  // VecAXPY(_material->_u, 1.0, _savedU);
  VecCopy(_fault_dyn->_slip, _varEx["slip"]);
  VecCopy(_varEx["slip"], _fault_qd->_slip);

  _fault_qd->_viewers.swap(_fault_dyn->_viewers);
  _material->changeBCTypes(_mat_qd_bcRType,_mat_qd_bcTType,_mat_qd_bcLType,_mat_qd_bcBType);
  // ierr = _material->_sbp->setRhs(_material->_rhs,_material->_bcL,_material->_bcR,_material->_bcT,_material->_bcB);CHKERRQ(ierr);

  _varEx.erase("u");
  _varEx.erase("uPrev");
  return ierr;
}

//===========================================================================================================
// Quasi dynamic stuff
//===========================================================================================================


// initiate variables to be integrated in time
PetscErrorCode StrikeSlip_PowerLaw_switch::initiateIntegrand_qd()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::initiateIntegrand_qd()";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  Mat A;
  _material->_sbp->getA(A);
  _material->setupKSP(A,_material->_ksp,_material->_pc);

  if (_isMMS) { _material->setMMSInitialConditions(_initTime); }

  if(_firstCycle){
    VecSet(_material->_bcR,_vL*_initTime/2.0);

    Vec slip;
    VecDuplicate(_material->_bcL,&slip);
    VecSet(slip,0.);
    _varEx["slip"] = slip;

    Vec psi;
    VecDuplicate(_material->_bcL,&psi);
    VecSet(psi,0.);
    _varEx["psi"] = psi;

    if (_guessSteadyStateICs) { solveSS(); }
     _material->initiateIntegrand(_initTime,_varEx);
    _fault_qd->initiateIntegrand(_initTime,_varEx);

      if (_thermalCoupling.compare("no")!=0 ) {
     _he->initiateIntegrand(_initTime,_varEx,_varIm);
     _fault_qd->updateTemperature(_he->_T);
  }

  if (_hydraulicCoupling.compare("no")!=0 ) {
     _p->initiateIntegrand(_initTime,_varEx,_varIm);
  }

    if (_inputDir.compare("unspecified") != 0){

      ierr = loadFileIfExists_matlab(_inputDir+"u", _material->_u);
      if (ierr > 0){
          VecSet(_material->_u, 0.0);
      }

      ierr = loadFileIfExists_matlab(_inputDir + "psi", _varEx["psi"]);
      ierr = loadFileIfExists_matlab(_inputDir + "slipVel", _fault_qd->_slipVel);
      ierr = loadFileIfExists_matlab(_inputDir + "bcR", _material->_bcRShift);
      ierr = loadFileIfExists_matlab(_inputDir + "bcL", _material->_bcL);

      VecCopy(_material->_bcL, _varEx["slip"]);
      VecScale(_varEx["slip"], 2.0);
      VecCopy(_varEx["slip"], _fault_qd->_slip);
    }
  }
  ierr = _material->_sbp->setRhs(_material->_rhs,_material->_bcL,_material->_bcR,_material->_bcT,_material->_bcB);CHKERRQ(ierr);
  _stride1D = _stride1D_qd;
  _stride2D = _stride2D_qd;
  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}


// monitoring function for explicit integration
PetscErrorCode StrikeSlip_PowerLaw_switch::timeMonitor_qd(const PetscScalar time,const PetscInt stepCount,
      const map<string,Vec>& varEx,const map<string,Vec>& dvarEx,int& stopIntegration)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::timeMonitor for explicit";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
double startTime = MPI_Wtime();

  _stepCount = stepCount;
    // Case we enter twice in the same time step
  if(time > _currTime){
    _dT = time - _currTime;
  }
  _currTime = time;

  PetscInt localStride1d, localStride2d;
  if (_localStep < _debug){
    localStride1d = 1;
    localStride2d = 1;
  }
  else{
    localStride1d = _stride1D;
    localStride2d = _stride2D;
  }

  if (_stride1D>0 && stepCount % localStride1d == 0) {
    ierr = writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _fault_qd->writeStep(_stepCount,time,_outputDir); CHKERRQ(ierr);
    if (_hydraulicCoupling.compare("no")!=0) { ierr = _p->writeStep(_stepCount,time,_outputDir); CHKERRQ(ierr); }
  }

  if (_stride2D>0 &&  stepCount % localStride2d == 0) {
    ierr = writeStep2D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep2D(_stepCount,time,_outputDir);CHKERRQ(ierr);
  }

  if(check_switch(_fault_qd)){
    if (_timeIntegrator.compare("RK32_WBE")==0 || _timeIntegrator.compare("RK43_WBE")==0) {
      _quadImex_qd->_maxNumSteps = 0;
    }
    else{
      _quadEx_qd->_maxNumSteps = 0;
    }
  }

_writeTime += MPI_Wtime() - startTime;
  #if VERBOSE > 0
    ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %.15e\n",stepCount,_currTime);CHKERRQ(ierr);
  #endif
  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// monitoring function for IMEX integration
PetscErrorCode StrikeSlip_PowerLaw_switch::timeMonitor_qd(const PetscScalar time,const PetscInt stepCount,
      const map<string,Vec>& varEx,const map<string,Vec>& dvarEx,const map<string,Vec>& varIm,int& stopIntegration)
{
  PetscErrorCode ierr = 0;

  _currTime = time;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::timeMonitor for IMEX";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
double startTime = MPI_Wtime();

  _stepCount = stepCount;
  // Case we enter twice in the same time step
  if(time > _currTime){
    _dT = time - _currTime;
  }
  _currTime = time;

  PetscInt localStride1d, localStride2d;
  if (_localStep < _debug){
    localStride1d = 1;
    localStride2d = 1;
  }
  else{
    localStride1d = _stride1D;
    localStride2d = _stride2D;
  }

  if (_stride1D>0 && stepCount % localStride1d == 0) {
    ierr = writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _fault_qd->writeStep(_stepCount,time,_outputDir); CHKERRQ(ierr);
    if (_hydraulicCoupling.compare("no")!=0) { ierr = _p->writeStep(_stepCount,time,_outputDir); CHKERRQ(ierr); }
    if (_thermalCoupling.compare("no")!=0) { ierr =  _he->writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr); }
  }

  if (_stride2D>0 &&  stepCount % localStride2d == 0) {
    ierr = writeStep2D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep2D(_stepCount,time,_outputDir);CHKERRQ(ierr);
    if (_thermalCoupling.compare("no")!=0) { ierr =  _he->writeStep2D(_stepCount,time,_outputDir);CHKERRQ(ierr); }
  }

  if(check_switch(_fault_qd)){
    if (_timeIntegrator.compare("RK32_WBE")==0 || _timeIntegrator.compare("RK43_WBE")==0) {
      _quadImex_qd->_maxNumSteps = 0;
    }
    else{
      _quadEx_qd->_maxNumSteps = 0;
    }
  }

  #if VERBOSE > 0
    ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %.15e\n",stepCount,_currTime);CHKERRQ(ierr);
  #endif
_writeTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::writeStep1D(const PetscInt stepCount, const PetscScalar time,const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::writeStep1D";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  if (_timeV1D==NULL) {
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(outputDir+"med_time1D.txt").c_str(),&_timeV1D);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_timeV1D, "%.15e\n",time);CHKERRQ(ierr);
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(outputDir+"med_dt1D.txt").c_str(),&_dtimeV1D);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_dtimeV1D, "%.15e\n",_dT);CHKERRQ(ierr);
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(outputDir+"regime.txt").c_str(),&_whichRegime);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_whichRegime, "%i\n",_inDynamic);CHKERRQ(ierr);
  }
  else {
    ierr = PetscViewerASCIIPrintf(_timeV1D, "%.15e\n",time);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_dtimeV1D, "%.15e\n",_dT);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_whichRegime, "%i\n",_inDynamic);CHKERRQ(ierr);
  }

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::writeStep2D(const PetscInt stepCount, const PetscScalar time,const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::writeStep1D";
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


PetscErrorCode StrikeSlip_PowerLaw_switch::view()
{
  PetscErrorCode ierr = 0;

  double totRunTime = MPI_Wtime() - _startTime;

  if (_timeIntegrator.compare("IMEX")==0&& _quadImex_qd!=NULL) { ierr = _quadImex_qd->view(); }
  if (_timeIntegrator.compare("RK32")==0 && _quadEx_qd!=NULL) { ierr = _quadEx_qd->view(); }

  _material->view(_integrateTime);
  _fault_qd->view(_integrateTime);
  if (_hydraulicCoupling.compare("no")!=0) { _p->view(_integrateTime); }
  if (_thermalCoupling.compare("no")!=0) { _he->view(); }

  ierr = PetscPrintf(PETSC_COMM_WORLD,"-------------------------------\n\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"StrikeSlip_PowerLaw_switch Runtime Summary:\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent in integration (s): %g\n",_integrateTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent writing output (s): %g\n",_writeTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent propagating the wave (s): %g\n",_propagateTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent in quasidynamic (s): %g\n",_qdTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent in dynamic (s): %g\n",_dynTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   total run time (s): %g\n",totRunTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   %% integration time spent writing output: %g\n",(_writeTime/_integrateTime)*100.);CHKERRQ(ierr);
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::writeContext()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::writeContext";
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
  ierr = PetscViewerASCIIPrintf(viewer,"maxStepCount = %i\n",_maxStepCount_qd);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"initTime = %.15e # (s)\n",_initTime);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxTime = %.15e # (s)\n",_maxTime_qd);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"minDeltaT = %.15e # (s)\n",_minDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxDeltaT = %.15e # (s)\n",_maxDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"initDeltaT = %.15e # (s)\n",_initDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"atol = %.15e\n",_atol);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"triggerqd2d = %.15e\n",_triggerqd2d);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"triggerd2qd = %.15e\n",_triggerd2qd);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"limit_qd = %.15e\n",_limit_qd);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"limit_dyn = %.15e\n",_limit_dyn);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"limit_stride_dyn = %.15e\n",_limit_stride_dyn);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"timeIntInds = %s\n",vector2str(_timeIntInds).c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"CFL = %.15e\n",_CFL);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"\n");CHKERRQ(ierr);

  PetscViewerDestroy(&viewer);

  _material->writeContext(_outputDir);
  _fault_qd->writeContext(_outputDir);
  if (_thermalCoupling.compare("no")!=0) {
    _he->writeContext(_outputDir);
  }
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

PetscErrorCode StrikeSlip_PowerLaw_switch::integrate_qd()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::integrate";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  double startTime = MPI_Wtime();

  initiateIntegrand_qd(); // put initial conditions into var for integration
  _stepCount = 0;

  // initialize time integrator
  if (_timeIntegrator.compare("FEuler")==0) {
    if (!_firstCycle){
      delete _quadEx_qd;
      delete _quadEx_switch;
    }
    _quadEx_qd = new FEuler(_maxStepCount_qd,_maxTime_qd,_initDeltaT,_timeControlType);
    if(!_firstCycle){
      _quadEx_qd->_stepCount = _quadWaveEx->_stepCount + 1;
      _quadEx_qd->_maxNumSteps = _maxStepCount_qd + _quadWaveEx->_stepCount + 1;
      _quadEx_qd->_currT = _currTime;
    }
    _quadEx_switch = new FEuler(1,_deltaT,_deltaT,_timeControlType);
  }
  else if (_timeIntegrator.compare("RK32")==0) {
    if (!_firstCycle){
      delete _quadEx_qd;
      delete _quadEx_switch;
    }
    _quadEx_qd = new RK32(_maxStepCount_qd,_maxTime_qd,_initDeltaT,_timeControlType);
    if(!_firstCycle){
      _quadEx_qd->_stepCount = _quadWaveEx->_stepCount + 1;
      _quadEx_qd->_maxNumSteps = _maxStepCount_qd + _quadWaveEx->_stepCount + 1;
      _quadEx_qd->_currT = _currTime;
    }
    _quadEx_switch = new RK32(1,_deltaT,_deltaT,_timeControlType);
  }
  else if (_timeIntegrator.compare("RK43")==0) {
    if (!_firstCycle){
      delete _quadEx_qd;
      delete _quadEx_switch;
    }
    _quadEx_qd = new RK43(_maxStepCount_qd,_maxTime_qd,_initDeltaT,_timeControlType);
    if(!_firstCycle){
      _quadEx_qd->_stepCount = _quadWaveEx->_stepCount + 1;
      _quadEx_qd->_maxNumSteps =_maxStepCount_qd +  _quadWaveEx->_stepCount + 1;
      _quadEx_qd->_currT = _currTime;
    }
    _quadEx_switch = new RK43(1,_deltaT,_deltaT,_timeControlType);
  }
  else if (_timeIntegrator.compare("RK32_WBE")==0) {
    if (!_firstCycle){
      delete _quadImex_qd;
      delete _quadImex_switch;
    }
    _quadImex_qd = new RK32_WBE(_maxStepCount_qd,_maxTime_qd,_initDeltaT,_timeControlType);
    if(!_firstCycle){
      _quadImex_qd->_stepCount = _quadWaveEx->_stepCount + 1;
      _quadImex_qd->_maxNumSteps = _maxStepCount_qd + _quadWaveEx->_stepCount + 1;
      _quadImex_qd->_currT = _currTime;
    }
    _quadImex_switch = new RK32_WBE(1,_deltaT,_deltaT,_timeControlType);
  }
  else if (_timeIntegrator.compare("RK43_WBE")==0) {
    if (!_firstCycle){
      delete _quadImex_qd;
      delete _quadImex_switch;
    }
    _quadImex_qd = new RK43_WBE(_maxStepCount_qd,_maxTime_qd,_initDeltaT,_timeControlType);
    if(!_firstCycle){
      _quadImex_qd->_stepCount = _quadWaveEx->_stepCount + 1;
      _quadImex_qd->_maxNumSteps = _maxStepCount_qd + _quadWaveEx->_stepCount + 1;
      _quadImex_qd->_currT = _currTime;
    }
    _quadImex_switch = new RK43_WBE(1,_deltaT,_deltaT,_timeControlType);
  }
  else {
    PetscPrintf(PETSC_COMM_WORLD,"ERROR: timeIntegrator type not understood\n");
    assert(0); // automatically fail
  }

  if (_timeIntegrator.compare("RK32_WBE")==0 || _timeIntegrator.compare("RK43_WBE")==0) {
    _quadImex_qd->setTolerance(_atol);CHKERRQ(ierr);
    _quadImex_qd->setTimeStepBounds(_minDeltaT,_maxDeltaT);CHKERRQ(ierr);
    ierr = _quadImex_qd->setTimeRange(_currTime,_maxTime_qd);
    ierr = _quadImex_qd->setToleranceType(_normType); CHKERRQ(ierr);
    ierr = _quadImex_qd->setInitialConds(_varEx,_varIm);CHKERRQ(ierr);
    ierr = _quadImex_qd->setErrInds(_timeIntInds); // control which fields are used to select step size
    if (_maxStepCount_qd > 0 && _startOnDynamic != 1){
      ierr = _quadImex_qd->integrate(this);CHKERRQ(ierr);
    }
  }
  else {
    _quadEx_qd->setTolerance(_atol);CHKERRQ(ierr);
    _quadEx_qd->setTimeStepBounds(_minDeltaT,_maxDeltaT);CHKERRQ(ierr);
    ierr = _quadEx_qd->setTimeRange(_currTime,_maxTime_qd);
    ierr = _quadEx_qd->setToleranceType(_normType); CHKERRQ(ierr);
    ierr = _quadEx_qd->setInitialConds(_varEx);CHKERRQ(ierr);
    ierr = _quadEx_qd->setErrInds(_timeIntInds); // control which fields are used to select step size
    if (_maxStepCount_qd > 0 && _startOnDynamic != 1){
      ierr = _quadEx_qd->integrate(this);CHKERRQ(ierr);
    }
  }
  _integrateTime = MPI_Wtime() - startTime;
  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// purely explicit time stepping
// note that the heat equation never appears here because it is only ever solved implicitly
PetscErrorCode StrikeSlip_PowerLaw_switch::d_dt_qd(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;

  // update for momBal; var holds slip, bcL is displacement at y=0+
  if (_qd_bcLType.compare("symm_fault")==0) {
    ierr = VecCopy(varEx.find("slip")->second,_material->_bcL);CHKERRQ(ierr);
    ierr = VecScale(_material->_bcL,0.5);CHKERRQ(ierr);
  }
  else if (_qd_bcLType.compare("rigid_fault")==0) {
    ierr = VecCopy(varEx.find("slip")->second,_material->_bcL);CHKERRQ(ierr);
  }
  ierr = VecSet(_material->_bcR,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_material->_bcR,1.0,_material->_bcRShift);CHKERRQ(ierr);

  if (_hydraulicCoupling.compare("coupled")==0 && varEx.find("pressure") != varEx.end() ) {
    _fault_qd->setSNEff(varEx.find("pressure")->second);
  }
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
  ierr = _fault_qd->setTauQS(sxy); CHKERRQ(ierr);

  // rates for fault
  ierr = _fault_qd->d_dt(time,varEx,dvarEx); // sets rates for slip and state

  return ierr;
}



// implicit/explicit time stepping
PetscErrorCode StrikeSlip_PowerLaw_switch::d_dt_qd(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx,
      map<string,Vec>& varIm,const map<string,Vec>& varImo,const PetscScalar dt)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::d_dt";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  // update state of each class from integrated variables varEx and varImo

  // update for momBal; var holds slip, bcL is displacement at y=0+
  if (_qd_bcLType.compare("symm_fault")==0) {
    ierr = VecCopy(varEx.find("slip")->second,_material->_bcL);CHKERRQ(ierr);
    ierr = VecScale(_material->_bcL,0.5);CHKERRQ(ierr);
  }
  else if (_qd_bcLType.compare("rigid_fault")==0) {
    ierr = VecCopy(varEx.find("slip")->second,_material->_bcL);CHKERRQ(ierr);
  }
  ierr = VecSet(_material->_bcR,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_material->_bcR,1.0,_material->_bcRShift);CHKERRQ(ierr);

  _fault_qd->updateFields(time,varEx);

  if ( varImo.find("pressure") != varImo.end() || varEx.find("pressure") != varEx.end()) {
    _p->updateFields(time,varEx,varImo);
  }

  // update temperature in momBal
  if (varImo.find("Temp") != varImo.end() && _thermalCoupling.compare("coupled")==0) {
    _fault_qd->updateTemperature(varImo.find("Temp")->second);
  }

  // update effective normal stress in fault using pore pressure
  if (_hydraulicCoupling.compare("coupled")==0) {
    _fault_qd->setSNEff(_p->_p);
  }

  // compute rates
  ierr = solveMomentumBalance(time,varEx,dvarEx); CHKERRQ(ierr);
  if ( varImo.find("pressure") != varImo.end() || varEx.find("pressure") != varEx.end()) {
    _p->d_dt(time,varEx,dvarEx,varIm,varImo,dt);
    // _p->d_dt(time,varEx,dvarEx);
  }

  // update shear stress on fault from momentum balance computation
  Vec sxy,sxz,sdev;
  ierr = _material->getStresses(sxy,sxz,sdev);
  ierr = _fault_qd->setTauQS(sxy); CHKERRQ(ierr);

  // rates for fault
  ierr = _fault_qd->d_dt(time,varEx,dvarEx); // sets rates for slip and state

  // heat equation
  if (varIm.find("Temp") != varIm.end()) {
    //~ PetscPrintf(PETSC_COMM_WORLD,"Computing new steady state temperature at stepCount = %i\n",_stepCount);
    Vec sxy,sxz,sdev;
    _material->getStresses(sxy,sxz,sdev);
    Vec V = dvarEx.find("slip")->second;
    Vec tau = _fault_qd->_tauP;
    Vec gVxy_t = NULL;
    Vec gVxz_t = NULL;
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
PetscErrorCode StrikeSlip_PowerLaw_switch::solveMomentumBalance(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;

  // compute source terms to rhs: d/dy(mu*gVxy) + d/dz(mu*gVxz)
  Vec viscSource;
  ierr = VecDuplicate(_material->_gxy,&viscSource);CHKERRQ(ierr);
  ierr = VecSet(viscSource,0.0);CHKERRQ(ierr);
  ierr = _material->computeViscStrainSourceTerms(viscSource,_material->_gxy,_material->_gxz); CHKERRQ(ierr);

  // set up rhs vector
  //~ if (_isMMS) { _material->setMMSBoundaryConditions(time); }
  _material->setRHS();
  ierr = VecAXPY(_material->_rhs,1.0,viscSource); CHKERRQ(ierr);
  VecDestroy(&viscSource);
  //~ if (_isMMS) { _material->addRHS_MMSSource(time,_material->_rhs); }

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
  //~ if (_isMMS) { _material->addViscStrainRates_MMSSource(time,dvarEx["gVxy"],dvarEx["gVxz"]); }

  return ierr;
}

// for solving fixed point iteration problem, with or without the heat equation
PetscErrorCode StrikeSlip_PowerLaw_switch::integrateSS()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::integrateSS";
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
  _material->initiateIntegrand(_initTime,_varEx);
  _fault_qd->initiateIntegrand(_initTime,_varEx);

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
      _quadEx_qd = new RK32(_maxSSIts_timesteps,_maxTime,_initDeltaT,_timeControlType);
    }
    else if (_timeIntegrator.compare("RK43")==0) {
      _quadEx_qd = new RK43(_maxSSIts_timesteps,_maxTime,_initDeltaT,_timeControlType);
    }
    else {
      PetscPrintf(PETSC_COMM_WORLD,"ERROR: time integrator time not acceptable for fixed point iteration method.\n");
      assert(0);
    }
    ierr = _quadEx_qd->setTolerance(_atol); CHKERRQ(ierr);
    ierr = _quadEx_qd->setTimeStepBounds(_minDeltaT,_maxDeltaT);CHKERRQ(ierr);
    ierr = _quadEx_qd->setTimeRange(_initTime,_maxTime); CHKERRQ(ierr);
    ierr = _quadEx_qd->setToleranceType(_normType); CHKERRQ(ierr);
    ierr = _quadEx_qd->setInitialConds(_varEx);CHKERRQ(ierr);
    ierr = _quadEx_qd->setErrInds(_timeIntInds);
    ierr = _quadEx_qd->integrate(this);CHKERRQ(ierr);
    delete _quadEx_qd; _quadEx_qd = NULL;

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
    _material->initiateIntegrand(_initTime,_varEx);
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
PetscErrorCode StrikeSlip_PowerLaw_switch::guessTauSS(map<string,Vec>& varSS)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::guessTauSS";
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


PetscErrorCode StrikeSlip_PowerLaw_switch::solveSS()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::solveSS";
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
PetscErrorCode StrikeSlip_PowerLaw_switch::solveSSViscoelasticProblem()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::solveSSViscoelasticProblem";
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

PetscErrorCode StrikeSlip_PowerLaw_switch::writeSS(const int Ii, const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::writeSS";
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
PetscErrorCode StrikeSlip_PowerLaw_switch::setSSBCs()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::setSSBCs";
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

  if (_varEx.find("slip") != _varEx.end() ) { VecCopy(uL,_varEx["slip"]); }
  else {
    Vec slip;
    VecDuplicate(_material->_bcL,&slip);
    VecCopy(uL,slip);
    _varEx["slip"] = slip;
  }
  if (_qd_bcLType.compare("symm_fault")==0) {
    VecScale(_varEx["slip"],2.);
  }

  VecDestroy(&uL);

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::measureMMSError()
{
  PetscErrorCode ierr = 0;

  _material->measureMMSError(_currTime);
  //~ _he->measureMMSError(_currTime);
  //~ _p->measureMMSError(_currTime);

  return ierr;
}


// ================================================================================================
// Dynamic part
// ================================================================================================

PetscErrorCode StrikeSlip_PowerLaw_switch::integrate_dyn()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::integrate";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  double startTime = MPI_Wtime();
  if(_maxStepCount_dyn > 0){
    initiateIntegrand_dyn(); // put initial conditions into var for integration
    _stepCount = 0;

    // initialize time integrator
    if(_maxStepCount_qd > 0 || _startOnDynamic){
    if (_timeIntegrator.compare("RK32_WBE")==0 || _timeIntegrator.compare("RK43_WBE")==0) {
      if (!_firstCycle){
        delete _quadWaveImex;
      }
        _quadWaveImex = new OdeSolver_WaveImex(_maxStepCount_dyn,_currTime+_deltaT,_maxTime_dyn,_deltaT);
        _quadWaveImex->_stepCount = _quadImex_qd->_stepCount + 3;
        _quadWaveImex->_maxNumSteps = _maxStepCount_dyn + _quadImex_qd->_stepCount + 3;
        ierr = _quadWaveImex->setInitialConds(_varEx, _varIm);CHKERRQ(ierr);
        ierr = _quadWaveImex->integrate(this);CHKERRQ(ierr);
      }
      else{
        if (!_firstCycle){
          delete _quadWaveEx;
        }
        _quadWaveEx = new OdeSolver_WaveEq(_maxStepCount_dyn,_currTime+_deltaT,_maxTime_dyn,_deltaT);
        _quadWaveEx->_stepCount = _quadEx_qd->_stepCount + 3;
        _quadWaveEx->_maxNumSteps = _maxStepCount_dyn + _quadEx_qd->_stepCount + 3;
        ierr = _quadWaveEx->setInitialConds(_varEx);CHKERRQ(ierr);
        ierr = _quadWaveEx->integrate(this);CHKERRQ(ierr);
      }
    }
    else{
      if (_timeIntegrator.compare("RK32_WBE")==0 || _timeIntegrator.compare("RK43_WBE")==0) {
        if (!_firstCycle){
          delete _quadWaveImex;
        }
        _quadWaveImex = new OdeSolver_WaveImex(_maxStepCount_dyn,_currTime+_deltaT,_maxTime_dyn,_deltaT);
        _quadWaveImex->_stepCount = _quadImex_qd->_stepCount + 2;
        _quadWaveImex->_maxNumSteps = _maxStepCount_dyn + _quadImex_qd->_stepCount + 2;
        ierr = _quadWaveImex->setInitialConds(_varEx, _varIm);CHKERRQ(ierr);
        ierr = _quadWaveImex->integrate(this);CHKERRQ(ierr);
      }
      else{
        if (!_firstCycle){
          delete _quadWaveEx;
        }
        _quadWaveEx = new OdeSolver_WaveEq(_maxStepCount_dyn,_currTime+_deltaT,_maxTime_dyn,_deltaT);
        _quadWaveEx->_stepCount = _quadEx_qd->_stepCount + 2;
        _quadWaveEx->_maxNumSteps = _maxStepCount_dyn + _quadEx_qd->_stepCount + 2;
        ierr = _quadWaveEx->setInitialConds(_varEx);CHKERRQ(ierr);
        ierr = _quadWaveEx->integrate(this);CHKERRQ(ierr);
      }
    }

    _integrateTime += MPI_Wtime() - startTime;

    reset_for_qd();
  }
  _firstCycle = false;
  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// purely explicit time stepping// note that the heat equation never appears here because it is only ever solved implicitly
PetscErrorCode StrikeSlip_PowerLaw_switch::d_dt_dyn(const PetscScalar time, map<string,Vec>& varEx,map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;
  // ierr = _material->_sbp->setRhs(_material->_rhs,_material->_bcL,_material->_bcR,_material->_bcT,_material->_bcB);CHKERRQ(ierr);
  Mat A;
  ierr = _material->_sbp->getA(A);

  double startPropagation = MPI_Wtime();

  // Update the laplacian
  Vec Laplacian, temp;
  VecDuplicate(*_y, &Laplacian);
  VecDuplicate(*_y, &temp);
  ierr = MatMult(A, varEx["u"], temp);
  if (_withFhat == 1){
    ierr = VecAXPY(temp, 1.0, _Fhat);
  }
  ierr = _material->_sbp->Hinv(temp, Laplacian);
  ierr = VecCopy(Laplacian, dvarEx["u"]);
  VecDestroy(&temp);
  VecDestroy(&Laplacian);

  if(_D->_sbpType.compare("mfc_coordTrans")==0){
    Mat J,Jinv,qy,rz,yq,zr;
    ierr = _material->_sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr); CHKERRQ(ierr);
    Vec temp;
    VecDuplicate(dvarEx["u"], &temp);
    MatMult(Jinv, dvarEx["u"], temp);
    VecCopy(temp, dvarEx["u"]);
    VecDestroy(&temp);
  }
  // Apply the time step
  Vec uNext, correction, previous, ones;

  VecDuplicate(varEx["u"], &ones);
  VecDuplicate(varEx["u"], &correction);
  VecSet(ones, 1.0);
  VecSet(correction, 0.0);
  ierr = VecAXPY(correction, _deltaT, _ay);
  ierr = VecAXPY(correction, -1.0, ones);
  VecDuplicate(varEx["u"], &previous);
  VecSet(previous, 0.0);
  ierr = VecPointwiseMult(previous, correction, varEx["uPrev"]);

  VecDuplicate(varEx["u"], &uNext);
  VecSet(uNext, 0.0);
  ierr = VecAXPY(uNext, pow(_deltaT, 2), dvarEx["u"]);
  ierr = VecPointwiseDivide(uNext, uNext, _rhoVec);

  ierr = VecAXPY(uNext, 2, varEx["u"]);
  ierr = VecAXPY(uNext, 1, previous);
  ierr = VecAXPY(correction, 2, ones);
  ierr = VecPointwiseDivide(uNext, uNext, correction);

  ierr = VecCopy(varEx["u"], varEx["uPrev"]);
  ierr = VecCopy(uNext, varEx["u"]);
  VecDestroy(&uNext);
  VecDestroy(&ones);
  VecDestroy(&correction);
  VecDestroy(&previous);
  if (_initialConditions.compare("tau")==0){
    PetscScalar currT;
    _quadWaveEx->getCurrT(currT);
    ierr = _fault_dyn->updateTau(currT);
  }
  _propagateTime += MPI_Wtime() - startPropagation;

  if (_isFault.compare("true") == 0){
  ierr = _fault_dyn->d_dt(time,varEx,dvarEx, _deltaT);CHKERRQ(ierr);
}
  VecCopy(varEx["u"], _material->_u);
  VecAXPY(_material->_u, 1.0, _savedU);
  _material->computeStresses();
  Vec sxy,sxz,sdev;
  ierr = _material->getStresses(sxy,sxz,sdev);
  ierr = _fault_dyn->setTauQS(sxy); CHKERRQ(ierr);
  VecCopy(_fault_dyn->_tauQSP, _fault_dyn->_tauP);

  if (_qd_bcLType.compare("symm_fault")==0) {
    ierr = VecCopy(_fault_dyn->_slip,_material->_bcL);CHKERRQ(ierr);
    ierr = VecScale(_material->_bcL,0.5);CHKERRQ(ierr);
  }
  else if (_qd_bcLType.compare("rigid_fault")==0) {
    ierr = VecCopy(_fault_dyn->_slip,_material->_bcL);CHKERRQ(ierr);
  }
  // ierr = VecSet(_material->_bcR,_vL*time/2.0);CHKERRQ(ierr);
  // ierr = VecAXPY(_material->_bcR,1.0,_material->_bcRShift);CHKERRQ(ierr);
  ierr = VecAXPY(_material->_bcR, 1.0, _bcrOffsetVector);

  return ierr;
}

// purely explicit time stepping// note that the heat equation never appears here because it is only ever solved implicitly
PetscErrorCode StrikeSlip_PowerLaw_switch::d_dt_dyn(const PetscScalar time, map<string,Vec>& varEx,map<string,Vec>& dvarEx,
                                                         map<string,Vec>& varIm,map<string,Vec>& varImo)
{
  PetscErrorCode ierr = 0;
  // ierr = _material->_sbp->setRhs(_material->_rhs,_material->_bcL,_material->_bcR,_material->_bcT,_material->_bcB);CHKERRQ(ierr);
  Mat A;
  ierr = _material->_sbp->getA(A);

  double startPropagation = MPI_Wtime();

  if (varImo.find("Temp") != varImo.end() && _thermalCoupling.compare("coupled")==0) {
    _fault_dyn->updateTemperature(varImo.find("Temp")->second);
    // _material->updateTemperature(varImo.find("Temp")->second);
  }

  // Update the laplacian
  Vec Laplacian, temp;
  VecDuplicate(*_y, &Laplacian);
  VecDuplicate(*_y, &temp);
  ierr = MatMult(A, varEx["u"], temp);
  ierr = _material->_sbp->Hinv(temp, Laplacian);
  ierr = VecCopy(Laplacian, dvarEx["u"]);
  VecDestroy(&temp);

  if(_D->_sbpType.compare("mfc_coordTrans")==0){
    Mat J,Jinv,qy,rz,yq,zr;
    ierr = _material->_sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr); CHKERRQ(ierr);
    Vec temp;
    // MatView(J, PETSC_VIEWER_STDOUT_WORLD);
    VecDuplicate(dvarEx["u"], &temp);
    MatMult(Jinv, dvarEx["u"], temp);
    VecCopy(temp, dvarEx["u"]);
    VecDestroy(&temp);
  }

  Vec uNext, correction, previous, ones;

  VecDuplicate(varEx["u"], &ones);
  VecDuplicate(varEx["u"], &correction);
  VecSet(ones, 1.0);
  VecSet(correction, 0.0);
  ierr = VecAXPY(correction, _deltaT, _ay);
  ierr = VecAXPY(correction, -1.0, ones);
  VecDuplicate(varEx["u"], &previous);
  VecSet(previous, 0.0);
  ierr = VecPointwiseMult(previous, correction, varEx["uPrev"]);

  VecDuplicate(varEx["u"], &uNext);
  VecSet(uNext, 0.0);
  ierr = VecAXPY(uNext, pow(_deltaT, 2), dvarEx["u"]);
  ierr = VecPointwiseDivide(uNext, uNext, _rhoVec);

  ierr = VecAXPY(uNext, 2, varEx["u"]);
  ierr = VecAXPY(uNext, 1, previous);
  ierr = VecAXPY(correction, 2, ones);
  ierr = VecPointwiseDivide(uNext, uNext, correction);

  ierr = VecCopy(varEx["u"], varEx["uPrev"]);
  ierr = VecCopy(uNext, varEx["u"]);
  VecDestroy(&uNext);
  VecDestroy(&ones);
  VecDestroy(&correction);
  VecDestroy(&previous);
  if (_initialConditions.compare("tau")==0){
    PetscScalar currT;
    _quadWaveImex->getCurrT(currT);
    ierr = _fault_dyn->updateTau(currT);
  }

  _propagateTime += MPI_Wtime() - startPropagation;

  if (_isFault.compare("true") == 0){
  ierr = _fault_dyn->d_dt(time,varEx,dvarEx, _deltaT);CHKERRQ(ierr);
}

  VecCopy(varEx["u"], _material->_u);
  _material->computeStresses();

  Vec sxy,sxz,sdev;
  ierr = _material->getStresses(sxy,sxz,sdev);
  ierr = _fault_dyn->setTauQS(sxy); CHKERRQ(ierr);
  VecCopy(_fault_dyn->_tauQSP, _fault_dyn->_tauP);
  VecAXPY(_fault_dyn->_tauP, 1.0, _fault_dyn->_tau0);

  // heat equation
  if (varIm.find("Temp") != varIm.end()) {
    Vec sxy,sxz,sdev;
    _material->getStresses(sxy,sxz,sdev);
    Vec V = _fault_dyn->_slipVel;
    Vec tau = _fault_dyn->_tauP;
    Vec Told = varImo.find("Temp")->second;
    ierr = _he->be(time,V,tau,NULL,NULL,NULL,varIm["Temp"],Told,_deltaT); CHKERRQ(ierr);
    // arguments: time, slipVel, txy, sigmadev, dgxy, dgxz, T, old T, dt
  }

  return ierr;
}


PetscErrorCode StrikeSlip_PowerLaw_switch::initiateIntegrand_dyn()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::initiateIntegrand()";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  if (_isMMS) { _material->setMMSInitialConditions(_currTime); }

  // For checking over the switching

  // Force writing output
  if(_stepCount % _stride1D > 0){
    PetscInt stride1d, stride2d;
    stride1d = _stride1D;
    stride2d = _stride2D;
    _stride1D = 1;
    _stride2D = 1;
    timeMonitor(_currTime, _stepCount, _varEx, _varEx, _stride1D);
    _stride1D = stride1d;
    _stride2D = stride2d;
  }

  VecDuplicate(_material->_bcR, &_bcrOffsetVector);
  VecSet(_bcrOffsetVector, _vL / 2.0 * _deltaT);

  Vec uPrev;
  VecDuplicate(_material->_u, &uPrev);
  VecCopy(_material->_u, uPrev);

  VecDuplicate(_material->_u, &_savedU);
  VecCopy(_material->_u, _savedU);

  Mat A;
  ierr = _material->_sbp->getA(A);
  VecDuplicate(_material->_u, &_Fhat);
  MatMult(A, _material->_u, _Fhat);
  VecAXPY(_Fhat, -1, _material->_rhs);

  if (_timeIntegrator.compare("RK32_WBE")==0 || _timeIntegrator.compare("RK43_WBE")==0) {
    VecCopy(_quadImex_qd->getVar()["psi"], _fault_dyn->_psiPrev);

    _varEx = _quadImex_qd->getVar();
    _varIm = _quadImex_qd->getVarIm();

    _quadImex_switch->setTolerance(_atol);CHKERRQ(ierr);
    _quadImex_switch->setTimeStepBounds(_deltaT,_deltaT);CHKERRQ(ierr);
    ierr = _quadImex_switch->setTimeRange(_currTime,_maxTime_qd);
    ierr = _quadImex_switch->setToleranceType(_normType); CHKERRQ(ierr);
    ierr = _quadImex_switch->setInitialConds(_varEx,_varIm);CHKERRQ(ierr);
    ierr = _quadImex_switch->setErrInds(_timeIntInds); // control which fields are used to select step size
    if(_maxStepCount_qd > 0 && _startOnDynamic != 1){
    _quadImex_switch->_stepCount = _quadImex_qd->_stepCount + 1;
    _quadImex_switch->_maxNumSteps += _quadImex_qd->_stepCount + 1;
    }
    ierr = _quadImex_switch->integrate(this);CHKERRQ(ierr);
  }
  else {
    VecCopy(_quadEx_qd->getVar()["psi"], _fault_dyn->_psiPrev);

    _varEx = _quadEx_qd->getVar();

    _quadEx_switch->setTolerance(_atol);CHKERRQ(ierr);
    _quadEx_switch->setTimeStepBounds(_deltaT,_deltaT);CHKERRQ(ierr);
    ierr = _quadEx_switch->setTimeRange(_currTime,_maxTime_qd);
    ierr = _quadEx_switch->setToleranceType(_normType); CHKERRQ(ierr);
    ierr = _quadEx_switch->setInitialConds(_varEx);CHKERRQ(ierr);
    ierr = _quadEx_switch->setErrInds(_timeIntInds); // control which fields are used to select step size
    if(_maxStepCount_qd > 0 && _startOnDynamic != 1){
    _quadEx_switch->_stepCount = _quadEx_qd->_stepCount + 1;
    _quadEx_switch->_maxNumSteps += _quadEx_qd->_stepCount + 1;
    }
    ierr = _quadEx_switch->integrate(this);CHKERRQ(ierr);
  }

  VecDuplicate(*_z, &_varEx["uPrev"]); VecSet(_varEx["uPrev"],0.);
  VecDuplicate(*_z, &_varEx["u"]); VecSet(_varEx["u"], 0.0);
  VecCopy(uPrev, _varEx["uPrev"]);
  VecCopy(_material->_u, _varEx["u"]);
  VecDestroy(&uPrev);

  VecCopy(_fault_qd->_slip, _fault_dyn->_slip0);

  _allowed = false;
  // _limit = 1.0;
  _inDynamic = true;
  _localStep = 0;

  _stride1D = _stride1D_dyn;
  _stride2D = _stride2D_dyn;

  _fault_qd->_viewers.swap(_fault_dyn->_viewers);
  // _fault_dyn->writeUOffset(_savedU, _firstCycle, _outputDir);

  _material->computeStresses();
  Vec sxy,sxz,sdev;
  ierr = _material->getStresses(sxy,sxz,sdev);
  ierr = _fault_dyn->setTauQS(sxy); CHKERRQ(ierr);
  VecCopy(_fault_dyn->_tauQSP, _fault_dyn->_tau0);

  _material->changeBCTypes(_mat_dyn_bcRType,_mat_dyn_bcTType,_mat_dyn_bcLType,_mat_dyn_bcBType);

  _fault_dyn->initiateIntegrand(_initTime,_varEx);
  Vec slip;
  VecDuplicate(_varEx["psi"], &slip); VecSet(slip,0.);
  _varEx["slip"] = slip;
  Vec dslip;
  VecDuplicate(_varEx["psi"], &dslip); VecSet(dslip,0.);
  _varEx["dslip"] = dslip;

  VecCopy(_fault_qd->_slipVel, _varEx["dslip"]);
  VecCopy(_quadEx_switch->getVar()["psi"], _fault_dyn->_psi);

  VecAXPY(_varEx["u"], -1.0, _varEx["uPrev"]);
  VecSet(_varEx["uPrev"], 0.0);

  PetscInt Ii,Istart,Iend;
  PetscInt Jj = 0;
    // Create matrix _ay
    VecDuplicate(*_z, &_ay);
    VecSet(_ay, 0.0);
    Vec _ay_temp, _az_temp;
    VecDuplicate(*_z, &_ay_temp);
    VecSet(_ay_temp, 0.0);
    VecDuplicate(*_z, &_az_temp);
    VecSet(_az_temp, 0.0);

    PetscScalar *ay, *alphay, *alphaz;
    VecGetOwnershipRange(*_y,&Istart,&Iend);
    VecGetArray(_ay,&ay);
    VecGetArray(_alphay, &alphay);
    VecGetArray(_alphaz, &alphaz);
    Jj = 0;

    for (Ii=Istart;Ii<Iend;Ii++) {
      ay[Jj] = 0;
      if ((Ii/_Nz == 0) && (_dyn_bcLType.compare("outGoingCharacteristics") == 0)){ay[Jj] += 0.5 / alphay[Jj];}
      if ((Ii/_Nz == _Ny-1) && (_dyn_bcRType.compare("outGoingCharacteristics") == 0)){ay[Jj] += 0.5 / alphay[Jj];}
      if ((Ii%_Nz == 0) && (_dyn_bcTType.compare("outGoingCharacteristics") == 0)){ay[Jj] += 0.5 / alphaz[Jj];}
      if (((Ii+1)%_Nz == 0) && (_dyn_bcBType.compare("outGoingCharacteristics") == 0)){ay[Jj] += 0.5 / alphaz[Jj];}
      Jj++;
    }

    VecRestoreArray(_ay,&ay);
    VecRestoreArray(_alphay, &alphay);
    VecRestoreArray(_alphaz, &alphaz);

    ierr = VecPointwiseMult(_ay, _ay, _cs);

  _fault_dyn->initiateIntegrand_dyn(_varEx, _rhoVec);

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}


// monitoring function for explicit integration
PetscErrorCode StrikeSlip_PowerLaw_switch::timeMonitor_dyn(const PetscScalar time,const PetscInt stepCount,
      const map<string,Vec>& varEx,const map<string,Vec>& dvarEx,int& stopIntegration)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::timeMonitor for explicit";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
double startTime = MPI_Wtime();

  _stepCount = stepCount;
  _currTime = time;
  _dT = _deltaT;

  PetscInt localStride1d, localStride2d;
  if (_localStep < _debug){
    localStride1d = 1;
    localStride2d = 1;
  }
  else{
    localStride1d = _stride1D;
    localStride2d = _stride2D;
  }

  if ( _stride1D > 0 && stepCount % localStride1d == 0) {
    ierr = writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _fault_dyn->writeStep(_stepCount,time,_outputDir); CHKERRQ(ierr);
  }

  if ( _stride2D > 0 && stepCount % localStride2d == 0) {
    ierr = writeStep2D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep2D(_stepCount,time,_outputDir);CHKERRQ(ierr);
  }

  if(check_switch(_fault_dyn)){
    _quadWaveEx->_maxNumSteps = 0;
  }

_writeTime += MPI_Wtime() - startTime;
  #if VERBOSE > 0
    ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %.15e\n",stepCount,_currTime);CHKERRQ(ierr);
  #endif
  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// monitoring function for IMEX integration
PetscErrorCode StrikeSlip_PowerLaw_switch::timeMonitor_dyn(const PetscScalar time,const PetscInt stepCount,
      const map<string,Vec>& varEx,const map<string,Vec>& dvarEx,const map<string,Vec>& varIm,int& stopIntegration)
{
  PetscErrorCode ierr = 0;

  _currTime = time;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::timeMonitor for IMEX";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif
double startTime = MPI_Wtime();

  _stepCount = stepCount;
  _currTime = time;
  _dT = _deltaT;

  PetscInt localStride1d, localStride2d;
  if (_localStep < _debug){
    localStride1d = 1;
    localStride2d = 1;
  }
  else{
    localStride1d = _stride1D;
    localStride2d = _stride2D;
  }
  if ( _stride1D > 0 && stepCount % localStride1d == 0) {
    ierr = writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep1D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _fault_dyn->writeStep(_stepCount,time,_outputDir); CHKERRQ(ierr);
  }

  if ( _stride2D > 0 && stepCount % localStride2d == 0) {
    ierr = writeStep2D(_stepCount,time,_outputDir); CHKERRQ(ierr);
    ierr = _material->writeStep2D(_stepCount,time,_outputDir);CHKERRQ(ierr);
  }

  if(check_switch(_fault_dyn)){
    _quadWaveEx->_maxNumSteps = 0;
  }

  #if VERBOSE > 0
    ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %.15e\n",stepCount,_currTime);CHKERRQ(ierr);
  #endif
_writeTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}


PetscErrorCode StrikeSlip_PowerLaw_switch::view_dyn()
{
  PetscErrorCode ierr = 0;

  double totRunTime = MPI_Wtime() - _startTime;

  _material->view(_integrateTime);
  _fault_dyn->view(_integrateTime);
  int num_proc;
  MPI_Comm_size(MPI_COMM_WORLD, &num_proc);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"-------------------------------\n\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Domain Summary:\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   Nz: %i\n",_Nz);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   Ny: %i\n",_Ny);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   number of processors: %i\n",num_proc);CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"-------------------------------\n\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"StrikeSlip_PowerLaw_switch Runtime Summary:\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent in integration (s): %g\n",_integrateTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent writing output (s): %g\n",_writeTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent propagating the wave (s): %g\n",_propagateTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   %% integration time spent writing output: %g\n",_writeTime/totRunTime*100.);CHKERRQ(ierr);
  return ierr;
}

PetscErrorCode StrikeSlip_PowerLaw_switch::writeContext_dyn()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "StrikeSlip_PowerLaw_switch::writeContext";
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

  // time integration settings
  ierr = PetscViewerASCIIPrintf(viewer,"timeIntegrator = %s\n",_timeIntegrator.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"timeControlType = %s\n",_timeControlType.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"stride1D = %i\n",_stride1D);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"stride2D = %i\n",_stride1D);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxStepCount = %i\n",_maxStepCount_dyn);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"initTime = %.15e # (s)\n",_initTime);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxTime = %.15e # (s)\n",_maxTime_dyn);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"deltaT = %.15e # (s)\n",_deltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"atol = %.15e\n",_atol);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"timeIntInds = %s\n",vector2str(_timeIntInds).c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"\n");CHKERRQ(ierr);

  PetscViewerDestroy(&viewer);

  _material->writeContext(_outputDir);
  _fault_dyn->writeContext(_outputDir);

  #if VERBOSE > 1
     PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}