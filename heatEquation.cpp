
#include "heatEquation.hpp"

#define FILENAME "heatEquation.cpp"


HeatEquation::HeatEquation(Domain& D)
: _D(&D),_order(D._order),_Ny(D._Ny),_Nz(D._Nz),
  _Ly(D._Ly),_Lz(D._Lz),_dy(D._dq),_dz(D._dr),_y(&D._y),_z(&D._z),
  _heatEquationType("transient"),_isMMS(D._isMMS),_loadICs(D._loadICs),
  _file(D._file),_outputDir(D._outputDir),_delim(D._delim),_inputDir(D._inputDir),
  _kFile("unspecified"),
  _rhoFile("unspecified"),_hFile("unspecified"),_cFile("unspecified"),
  _kTz_z0(NULL),_kTz(NULL),
  _wViscShearHeating("yes"),_wFrictionalHeating("yes"),_wRadioHeatGen("yes"),
  _sbpType(D._sbpType),_sbp(NULL),
  _bcT(NULL),_bcR(NULL),_bcB(NULL),_bcL(NULL),
  _linSolver("CG"),_kspTol(1e-9),
  _kspSS(NULL),_kspTrans(NULL),_pc(NULL),_I(NULL),_rcInv(NULL),_B(NULL),_pcMat(NULL),_D2ath(NULL),
  _MapV(NULL),_Gw(NULL),_omega(NULL),_w(NULL),
  _linSolveTime(0),_factorTime(0),_beTime(0),_writeTime(0),_miscTime(0),
  _linSolveCount(0),
  _dT(NULL),_T(NULL),_Tamb(NULL),_k(NULL),_rho(NULL),_c(NULL),_Qrad(NULL),_Q(NULL)
{
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::HeatEquation";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  loadSettings(_file);
  checkInput();
  setFields();
  if (_wFrictionalHeating.compare("yes")==0) { constructMapV(); }
  loadFieldsFromFiles();
  if (!_isMMS && _loadICs!=1) { computeInitialSteadyStateTemp(); }


  if (_heatEquationType.compare("transient")==0 ) { setUpTransientProblem(); }
  else if (_heatEquationType.compare("steadyState")==0 ) { setUpSteadyStateProblem(); }


  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
}

HeatEquation::~HeatEquation()
{
  KSPDestroy(&_kspSS);
  KSPDestroy(&_kspTrans);
  MatDestroy(&_B);
  MatDestroy(&_rcInv);
  MatDestroy(&_I);
  MatDestroy(&_D2ath);
  MatDestroy(&_pcMat);

  MatDestroy(&_MapV);
  VecDestroy(&_Gw);
  VecDestroy(&_omega);
  VecDestroy(&_Q);

  VecDestroy(&_k);
  VecDestroy(&_rho);
  VecDestroy(&_c);
  VecDestroy(&_Qrad);

  VecDestroy(&_dT);
  VecDestroy(&_Tamb);
  VecDestroy(&_T);
  VecDestroy(&_kTz);
  VecDestroy(&_kTz_z0);
  VecDestroy(&_bcL);
  VecDestroy(&_bcR);
  VecDestroy(&_bcT);
  VecDestroy(&_bcB);
  VecDestroy(&_w);

  for (map<string,std::pair<PetscViewer,string> >::iterator it=_viewers.begin(); it!=_viewers.end(); it++ ) {
    PetscViewerDestroy(&_viewers[it->first].first);
  }

  delete _sbp;
}



// return temperature
PetscErrorCode HeatEquation::getTemp(Vec& T)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::getTemp()";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  //~ VecWAXPY(T,1.0,_Tamb,_dT);
  VecCopy(_T,T);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// set temperature
PetscErrorCode HeatEquation::setTemp(Vec& T)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::setTemp()";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  VecCopy(T,_T);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}



// loads settings from the input text file
PetscErrorCode HeatEquation::loadSettings(const char *file)
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

    if (var.compare("heatEquationType")==0) {
      _heatEquationType = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("withViscShearHeating")==0) {
      _wViscShearHeating = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("withFrictionalHeating")==0) {
      _wFrictionalHeating = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("withRadioHeatGeneration")==0) {
      _wRadioHeatGen = line.substr(pos+_delim.length(),line.npos).c_str();
    }

    // linear solver settings
    else if (var.compare("linSolver_heateq")==0) {
      _linSolver = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("kspTol_heateq")==0) {
      _kspTol = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
      }

    // names of each field's source file
    else if (var.compare("rhoFile")==0) {
      _rhoFile = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("kFile")==0) {
      _kFile = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("hFile")==0) {
      _hFile = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("cFile")==0) {
      _cFile = line.substr(pos+_delim.length(),line.npos).c_str();
    }

    // if values are set by vector
    else if (var.compare("rhoVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_rhoVals);
    }
    else if (var.compare("rhoDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_rhoDepths);
    }

    else if (var.compare("kVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_kVals);
    }
    else if (var.compare("kDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_kDepths);
    }

    else if (var.compare("hVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_hVals);
    }
    else if (var.compare("hDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_hDepths);
    }

    else if (var.compare("cVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_cVals);
    }
    else if (var.compare("cDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_cDepths);
    }

    else if (var.compare("TVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_TVals);
    }
    else if (var.compare("TDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_TDepths);
    }



    else if (var.compare("initTime")==0) {
      _initTime = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }
    else if (var.compare("initDeltaT")==0) {
      _initDeltaT = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }

    // finite width shear zone
    //~ else if (var.compare("shearZoneWidth")==0) {
      //~ _w = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    //~ }
    else if (var.compare("wVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_wVals);
    }
    else if (var.compare("wDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_wDepths);
    }

    // radioactive heat generation
    else if (var.compare("he_A0Vals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_A0Vals);
    }
    else if (var.compare("he_A0Depths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_A0Depths);
    }
    else if (var.compare("he_Lrad")==0) {
      _Lrad = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() );
    }
  }

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

//parse input file and load values into data members
PetscErrorCode HeatEquation::loadFieldsFromFiles()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::loadFieldsFromFiles()";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  // material properties
  ierr = loadVecFromInputFile(_rho,_inputDir,"rho"); CHKERRQ(ierr);
  ierr = loadVecFromInputFile(_k,_inputDir,"k"); CHKERRQ(ierr);
  ierr = loadVecFromInputFile(_Qrad,_inputDir,"h"); CHKERRQ(ierr);
  ierr = loadVecFromInputFile(_c,_inputDir,"c"); CHKERRQ(ierr);

  bool chkTamb = 0, chkT = 0, chkdT = 0;

  // load Tamb (background geotherm)
  ierr = loadVecFromInputFile(_Tamb,_inputDir,"Tamb",chkTamb); CHKERRQ(ierr);
  if (chkTamb) { _loadICs = 1; }

  // load T
  ierr = loadVecFromInputFile(_T,_inputDir,"T",chkT); CHKERRQ(ierr);

  if (chkT!=1 && chkTamb) { VecCopy(_Tamb,_T); }

  // load dT (perturbation from ambient geotherm)
  ierr = loadVecFromInputFile(_dT,_inputDir,"dT",chkdT); CHKERRQ(ierr);
  if (chkdT!=1 && chkTamb) { VecWAXPY(_dT,-1.0,_Tamb,_T); }

  if (chkTamb) { // set up boundary conditions
    VecScatterBegin(_D->_scatters["body2T"], _Tamb, _bcT, INSERT_VALUES, SCATTER_FORWARD);
    VecScatterEnd(_D->_scatters["body2T"], _Tamb, _bcT, INSERT_VALUES, SCATTER_FORWARD);

    VecScatterBegin(_D->_scatters["body2B"], _Tamb, _bcB, INSERT_VALUES, SCATTER_FORWARD);
    VecScatterEnd(_D->_scatters["body2B"], _Tamb, _bcB, INSERT_VALUES, SCATTER_FORWARD);

    VecScatterBegin(_D->_scatters["body2R"], _Tamb, _bcR, INSERT_VALUES, SCATTER_FORWARD);
    VecScatterEnd(_D->_scatters["body2R"], _Tamb, _bcR, INSERT_VALUES, SCATTER_FORWARD);
  }

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


// initialize all fields
PetscErrorCode HeatEquation::setFields()
{
PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::setFields";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  // set boundary conditions
  VecDuplicate(_D->_z0,&_bcT);
  PetscObjectSetName((PetscObject) _bcT, "_bcT");
  PetscScalar bcTval = (_TVals[1] - _TVals[0])/(_TDepths[1]-_TDepths[0]) * (0-_TDepths[0]) + _TVals[0];
  VecSet(_bcT,bcTval);

  VecDuplicate(_bcT,&_bcB); PetscObjectSetName((PetscObject) _bcB, "bcB");
  PetscScalar bcBval = (_TVals[1] - _TVals[0])/(_TDepths[1]-_TDepths[0]) * (_Lz-_TDepths[0]) + _TVals[0];
  VecSet(_bcB,bcBval);

  VecDuplicate(_D->_y0,&_bcR);
  PetscObjectSetName((PetscObject) _bcR, "_bcR");
  VecSet(_bcR,0.0);

  VecDuplicate(_bcR,&_bcL); PetscObjectSetName((PetscObject) _bcL, "_bcL");
  VecSet(_bcL,0.0);


  // set material properties
  VecDuplicate(*_y,&_k);
  VecDuplicate(_k,&_rho);
  VecDuplicate(_k,&_c);
  VecDuplicate(_k,&_Qrad); VecSet(_Qrad,0.);
  VecDuplicate(_k,&_dT); VecSet(_dT,0.);
  VecDuplicate(_k,&_Q); VecSet(_Q,0.);
  VecDuplicate(_k,&_omega); VecSet(_omega,0.);

  VecDuplicate(_dT,&_Tamb); VecSet(_Tamb,0.0);
  VecDuplicate(_dT,&_T); VecCopy(_Tamb,_T);

  // heat flux variables
  VecDuplicate(_bcT,&_kTz_z0); VecSet(_kTz_z0,0.0);
  VecDuplicate(_k,&_kTz); VecSet(_kTz,0.0);

  // set each field using it's vals and depths std::vectors
  if (_isMMS) {
    mapToVec(_k,zzmms_k,*_y,*_z);
    mapToVec(_rho,zzmms_rho,*_y,*_z);
    mapToVec(_c,zzmms_c,*_y,*_z);
    mapToVec(_Qrad,zzmms_h,*_y,*_z);
    mapToVec(_Tamb,zzmms_T,*_y,*_z,_initTime);
    mapToVec(_dT,zzmms_dT,*_y,*_z,_initTime);
    setMMSBoundaryConditions(_initTime,"Dirichlet","Dirichlet","Dirichlet","Dirichlet");
  }
  else {
    ierr = setVecFromVectors(_k,_kVals,_kDepths); CHKERRQ(ierr);
    ierr = setVecFromVectors(_rho,_rhoVals,_rhoDepths); CHKERRQ(ierr);
    //~ ierr = setVecFromVectors(_Qrad,_hVals,_hDepths); CHKERRQ(ierr);
    ierr = setVecFromVectors(_c,_cVals,_cDepths); CHKERRQ(ierr);
  }

  // set up radioactive heat generation source term
  // Qrad = A0 * exp(-z/Lrad)
  if (_wRadioHeatGen.compare("yes") == 0) {
    Vec A0; VecDuplicate(_Qrad,&A0);
    ierr = setVecFromVectors(A0,_A0Vals,_A0Depths); CHKERRQ(ierr);
    VecCopy(*_z,_Qrad);
    VecScale(_Qrad,-1.0/_Lrad);
    VecExp(_Qrad);
    VecPointwiseMult(_Qrad,A0,_Qrad);
    VecDestroy(&A0);
  }
  else { VecSet(_Qrad,0.); }



  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// Check that required fields have been set by the input file
PetscErrorCode HeatEquation::checkInput()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::checkInput";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  assert(_heatEquationType.compare("transient")==0 ||
      _heatEquationType.compare("steadyState")==0 );

  assert(_kVals.size() == _kDepths.size() );
  assert(_rhoVals.size() == _rhoDepths.size() );
  assert(_hVals.size() == _hDepths.size() );
  assert(_cVals.size() == _cDepths.size() );
  assert(_TVals.size() == _TDepths.size() );
  assert(_wVals.size() == _wDepths.size() );
  //~ assert(_TVals.size() == 2 );

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


PetscErrorCode HeatEquation::constructMapV()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::constructMapV";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  MatCreate(PETSC_COMM_WORLD,&_MapV);
  MatSetSizes(_MapV,PETSC_DECIDE,PETSC_DECIDE,_Ny*_Nz,_Nz);
  MatSetFromOptions(_MapV);
  MatMPIAIJSetPreallocation(_MapV,_Ny*_Nz,NULL,_Ny*_Nz,NULL);
  MatSeqAIJSetPreallocation(_MapV,_Ny*_Nz,NULL);
  MatSetUp(_MapV);

  PetscScalar v=1.0;
  PetscInt Ii=0,Istart=0,Iend=0,Jj=0;
  MatGetOwnershipRange(_MapV,&Istart,&Iend);
  for (Ii = Istart; Ii < Iend; Ii++) {
    Jj = Ii % _Nz;
    MatSetValues(_MapV,1,&Ii,1,&Jj,&v,INSERT_VALUES);
  }
  MatAssemblyBegin(_MapV,MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(_MapV,MAT_FINAL_ASSEMBLY);

  // construct Gw = exp(-y^2/(2*w)) / sqrt(2*pi)/w
  VecDuplicate(_Tamb,&_Gw); VecSet(_Gw,0.);
  VecDuplicate(_Tamb,&_w);
  if (_wVals.size() > 0 ) {
    ierr = setVecFromVectors(_w,_wVals,_wDepths); CHKERRQ(ierr);
    VecScale(_w,1e-3); // convert from m to km
  }
  else { VecSet(_w,0.); }
  VecMax(_w,NULL,&_wMax);

  PetscScalar const *y,*w;
  PetscScalar *g;
  VecGetOwnershipRange(_Gw,&Istart,&Iend);
  VecGetArrayRead(*_y,&y);
  VecGetArrayRead(_w,&w);
  VecGetArray(_Gw,&g);
  Jj = 0;
  for (Ii=Istart;Ii<Iend;Ii++) {
    g[Jj] = exp(-y[Jj]*y[Jj] / (2.*w[Jj]*w[Jj])) / sqrt(2. * M_PI) / w[Jj];
    Jj++;
  }
  VecRestoreArrayRead(*_y,&y);
  VecRestoreArrayRead(_w,&w);
  VecRestoreArray(_Gw,&g);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


// compute T assuming that dT/dt and viscous strain rates = 0
PetscErrorCode HeatEquation::computeInitialSteadyStateTemp()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::computeSteadyStateTemp";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  delete _sbp;

  if (_sbpType.compare("mc")==0) {
    _sbp = new SbpOps_c(_order,_Ny,_Nz,_Ly,_Lz,_k);
  }
  else if (_sbpType.compare("mfc")==0) {
    _sbp = new SbpOps_fc(_order,_Ny,_Nz,_Ly,_Lz,_k);
  }
  else if (_sbpType.compare("mfc_coordTrans")==0) {
    _sbp = new SbpOps_fc_coordTrans(_order,_Ny,_Nz,_Ly,_Lz,_k);
    if (_Ny > 1 && _Nz > 1) { _sbp->setGrid(_y,_z); }
    else if (_Ny == 1 && _Nz > 1) { _sbp->setGrid(NULL,_z); }
    else if (_Ny > 1 && _Nz == 1) { _sbp->setGrid(_y,NULL); }
  }
  else {
    PetscPrintf(PETSC_COMM_WORLD,"ERROR: SBP type type not understood\n");
    assert(0); // automatically fail
  }
  _sbp->setBCTypes("Dirichlet","Dirichlet","Dirichlet","Dirichlet");
  _sbp->setMultiplyByH(1);
  _sbp->setLaplaceType("z");
  _sbp->setDeleteIntermediateFields(1);
  _sbp->computeMatrices(); // actually create the matrices


  // radioactive heat generation source term
  Vec QradR,Qtemp;
  if (_wRadioHeatGen.compare("yes") == 0) {
    Vec temp1; VecDuplicate(_Qrad,&Qtemp);
    if (_sbpType.compare("mfc_coordTrans")==0) {
      Vec temp1; VecDuplicate(_Qrad,&temp1);
      Mat J,Jinv,qy,rz,yq,zr;
      ierr = _sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr); CHKERRQ(ierr);
      ierr = MatMult(J,_Qrad,temp1);
      Mat H; _sbp->getH(H);
      ierr = MatMult(H,temp1,Qtemp);
      VecDestroy(&temp1);
    }
    else{
      Mat H; _sbp->getH(H);
      ierr = MatMult(H,_Qrad,Qtemp); CHKERRQ(ierr);
    }
  }


  if (_Nz > 1) {

    Mat A; _sbp->getA(A);
    setupKSP_SS(A);

    Vec rhs;
    VecDuplicate(_k,&rhs);
    _sbp->setRhs(rhs,_bcL,_bcR,_bcT,_bcB);
    if (_wRadioHeatGen.compare("yes") == 0) {
      VecAXPY(rhs,-1.0,Qtemp);
      VecDestroy(&Qtemp);
    }

    // solve for temperature
    VecSet(_Tamb,0.);
    double startTime = MPI_Wtime();
    ierr = KSPSolve(_kspSS,rhs,_Tamb);CHKERRQ(ierr);
    _linSolveTime += MPI_Wtime() - startTime;
    _linSolveCount++;

    VecDestroy(&rhs);
    KSPDestroy(&_kspSS); _kspSS = NULL;
  }
  else{
    VecSet(_Tamb,_TVals[0]);
  }
  VecSet(_dT,0.0);
  VecCopy(_Tamb,_T);
  computeHeatFlux();

  // set up boundary conditions
  VecScatterBegin(_D->_scatters["body2T"], _Tamb, _bcT, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd(_D->_scatters["body2T"], _Tamb, _bcT, INSERT_VALUES, SCATTER_FORWARD);

  VecScatterBegin(_D->_scatters["body2B"], _Tamb, _bcB, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd(_D->_scatters["body2B"], _Tamb, _bcB, INSERT_VALUES, SCATTER_FORWARD);

  VecScatterBegin(_D->_scatters["body2R"], _Tamb, _bcR, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd(_D->_scatters["body2R"], _Tamb, _bcR, INSERT_VALUES, SCATTER_FORWARD);

  VecSet(_bcL,0);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// Solve steady-state heat equation
PetscErrorCode HeatEquation::setupKSP_SS(Mat& A)
{
  PetscErrorCode ierr = 0;

  #if VERBOSE > 1
    std::string funcName = "HeatEquation::setupKSP_SS";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  if (_kspSS != NULL) { return ierr; }


  // use MUMPSCHOLESKY
  //~ ierr = KSPCreate(PETSC_COMM_WORLD,&_kspSS); CHKERRQ(ierr);
  //~ ierr = KSPSetType(_kspSS,KSPPREONLY);CHKERRQ(ierr);
  //~ ierr = KSPSetOperators(_kspSS,A,A);CHKERRQ(ierr);
  //~ ierr = KSPSetReusePreconditioner(_kspSS,PETSC_TRUE);CHKERRQ(ierr);
  //~ PC pc;
  //~ ierr = KSPGetPC(_kspSS,&pc);CHKERRQ(ierr);
  //~ PCSetType(pc,PCCHOLESKY);
  //~ PCFactorSetMatSolverPackage(pc,MATSOLVERMUMPS);
  //~ PCFactorSetUpMatSolverPackage(pc);


  ierr = KSPCreate(PETSC_COMM_WORLD,&_kspSS); CHKERRQ(ierr);
  PC pc;
  if (_linSolver.compare("AMG")==0) { // algebraic multigrid from HYPRE
    // uses HYPRE's solver AMG (not HYPRE's preconditioners)
    ierr = KSPSetType(_kspSS,KSPRICHARDSON); CHKERRQ(ierr);
    ierr = KSPSetOperators(_kspSS,A,A); CHKERRQ(ierr);
    ierr = KSPSetReusePreconditioner(_kspSS,PETSC_TRUE); CHKERRQ(ierr); // necessary for solving steady state power law
    ierr = KSPGetPC(_kspSS,&pc); CHKERRQ(ierr);
    ierr = PCSetType(pc,PCHYPRE); CHKERRQ(ierr);
    ierr = PCHYPRESetType(pc,"boomeramg"); CHKERRQ(ierr);
    ierr = KSPSetTolerances(_kspSS,_kspTol,_kspTol,PETSC_DEFAULT,PETSC_DEFAULT); CHKERRQ(ierr);
    ierr = PCFactorSetLevels(pc,4); CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(_kspSS,PETSC_TRUE); CHKERRQ(ierr);
  }
  else if (_linSolver.compare("MUMPSLU")==0) { // direct LU from MUMPS
    // use direct LU from MUMPS
    ierr = KSPSetType(_kspSS,KSPPREONLY); CHKERRQ(ierr);
    ierr = KSPSetOperators(_kspSS,A,A); CHKERRQ(ierr);
    ierr = KSPSetReusePreconditioner(_kspSS,PETSC_TRUE); CHKERRQ(ierr);
    ierr = KSPGetPC(_kspSS,&pc); CHKERRQ(ierr);
    ierr = PCSetType(pc,PCLU); CHKERRQ(ierr);
    ierr = PCFactorSetMatSolverPackage(pc,MATSOLVERMUMPS); CHKERRQ(ierr);
    ierr = PCFactorSetUpMatSolverPackage(pc); CHKERRQ(ierr);
  }
  else if (_linSolver.compare("MUMPSCHOLESKY")==0) { // direct Cholesky (RR^T) from MUMPS
    // use direct LL^T (Cholesky factorization) from MUMPS
    ierr = KSPSetType(_kspSS,KSPPREONLY); CHKERRQ(ierr);
    ierr = KSPSetOperators(_kspSS,A,A); CHKERRQ(ierr);
    ierr = KSPSetReusePreconditioner(_kspSS,PETSC_TRUE); CHKERRQ(ierr);
    ierr = KSPGetPC(_kspSS,&pc); CHKERRQ(ierr);
    ierr = PCSetType(pc,PCCHOLESKY); CHKERRQ(ierr);
    ierr = PCFactorSetMatSolverPackage(pc,MATSOLVERMUMPS); CHKERRQ(ierr);
    ierr = PCFactorSetUpMatSolverPackage(pc); CHKERRQ(ierr);
  }
  else if (_linSolver.compare("CG")==0) { // conjugate gradient
    ierr = KSPSetType(_kspSS,KSPCG); CHKERRQ(ierr);
    ierr = KSPSetOperators(_kspSS,A,A); CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(_kspSS, PETSC_TRUE);
    ierr = KSPSetReusePreconditioner(_kspSS,PETSC_TRUE); CHKERRQ(ierr);
    ierr = KSPGetPC(_kspSS,&pc); CHKERRQ(ierr);
    ierr = KSPSetTolerances(_kspSS,_kspTol,_kspTol,PETSC_DEFAULT,PETSC_DEFAULT); CHKERRQ(ierr);
    ierr = PCSetType(pc,PCHYPRE); CHKERRQ(ierr);
    ierr = PCFactorSetShiftType(pc,MAT_SHIFT_POSITIVE_DEFINITE); CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(_kspSS,PETSC_TRUE); CHKERRQ(ierr);
  }
  else {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"ERROR: linSolver type not understood\n");
    assert(0);
  }


  double startTime = MPI_Wtime();
  // finish setting up KSP context using options defined above
  ierr = KSPSetFromOptions(_kspSS);CHKERRQ(ierr);

  // perform computation of preconditioners now, rather than on first use
  ierr = KSPSetUp(_kspSS);CHKERRQ(ierr);
  _factorTime += MPI_Wtime() - startTime;

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// set up KSP for transient problem
PetscErrorCode HeatEquation::setupKSP(Mat& A)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::setupKSP";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif


    // don't reuse preconditioner
    //~ ierr = KSPCreate(PETSC_COMM_WORLD,&_kspTrans); CHKERRQ(ierr);
    //~ ierr = KSPSetType(_kspTrans,KSPRICHARDSON);CHKERRQ(ierr);
    //~ ierr = KSPSetOperators(_kspTrans,A,A);CHKERRQ(ierr);
    //~ ierr = KSPSetReusePreconditioner(_kspTrans,PETSC_FALSE);CHKERRQ(ierr);
    //~ ierr = KSPGetPC(_kspTrans,&_pc);CHKERRQ(ierr);
    //~ ierr = PCSetType(_pc,PCHYPRE);CHKERRQ(ierr);
    //~ ierr = PCHYPRESetType(_pc,"boomeramg");CHKERRQ(ierr);
    //~ ierr = KSPSetTolerances(_kspTrans,_kspTol,_kspTol,PETSC_DEFAULT,PETSC_DEFAULT);CHKERRQ(ierr);
    //~ ierr = PCFactorSetLevels(_pc,4);CHKERRQ(ierr);
    //~ ierr = KSPSetInitialGuessNonzero(_kspTrans,PETSC_TRUE);CHKERRQ(ierr);

  ierr = KSPCreate(PETSC_COMM_WORLD,&_kspTrans); CHKERRQ(ierr);
  if (_linSolver.compare("AMG")==0) { // algebraic multigrid from HYPRE
    // uses HYPRE's solver AMG (not HYPRE's preconditioners)
    ierr = KSPSetType(_kspTrans,KSPRICHARDSON); CHKERRQ(ierr);
    ierr = KSPSetOperators(_kspTrans,A,A); CHKERRQ(ierr);
    ierr = KSPSetReusePreconditioner(_kspTrans,PETSC_TRUE); CHKERRQ(ierr);
    ierr = KSPGetPC(_kspTrans,&_pc); CHKERRQ(ierr);
    ierr = PCSetType(_pc,PCHYPRE); CHKERRQ(ierr);
    ierr = PCHYPRESetType(_pc,"boomeramg"); CHKERRQ(ierr);
    ierr = KSPSetTolerances(_kspTrans,_kspTol,_kspTol,PETSC_DEFAULT,PETSC_DEFAULT); CHKERRQ(ierr);
    ierr = PCFactorSetLevels(_pc,4); CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(_kspTrans,PETSC_TRUE); CHKERRQ(ierr);
  }
  else if (_linSolver.compare("MUMPSLU")==0) { // direct LU from MUMPS
    // use direct LU from MUMPS
    ierr = KSPSetType(_kspTrans,KSPPREONLY); CHKERRQ(ierr);
    ierr = KSPSetOperators(_kspTrans,A,A); CHKERRQ(ierr);
    ierr = KSPSetReusePreconditioner(_kspTrans,PETSC_TRUE); CHKERRQ(ierr);
    ierr = KSPGetPC(_kspTrans,&_pc); CHKERRQ(ierr);
    ierr = PCSetType(_pc,PCLU); CHKERRQ(ierr);
    ierr = PCFactorSetMatSolverPackage(_pc,MATSOLVERMUMPS); CHKERRQ(ierr);
    ierr = PCFactorSetUpMatSolverPackage(_pc); CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(_kspTrans,PETSC_TRUE); CHKERRQ(ierr);
  }
  else if (_linSolver.compare("MUMPSCHOLESKY")==0) { // direct Cholesky (RR^T) from MUMPS
    // use direct LL^T (Cholesky factorization) from MUMPS
    ierr = KSPSetType(_kspTrans,KSPPREONLY); CHKERRQ(ierr);
    ierr = KSPSetOperators(_kspTrans,A,A); CHKERRQ(ierr);
    ierr = KSPSetReusePreconditioner(_kspTrans,PETSC_TRUE); CHKERRQ(ierr);
    ierr = KSPGetPC(_kspTrans,&_pc); CHKERRQ(ierr);
    ierr = PCSetType(_pc,PCCHOLESKY); CHKERRQ(ierr);
    ierr = PCFactorSetMatSolverPackage(_pc,MATSOLVERMUMPS); CHKERRQ(ierr);
    ierr = PCFactorSetUpMatSolverPackage(_pc); CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(_kspTrans,PETSC_TRUE); CHKERRQ(ierr);
  }
  else if (_linSolver.compare("CG")==0) { // conjugate gradient
    ierr = KSPSetType(_kspTrans,KSPCG); CHKERRQ(ierr);
    ierr = KSPSetOperators(_kspTrans,A,A); CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(_kspTrans, PETSC_TRUE);
    ierr = KSPSetReusePreconditioner(_kspTrans,PETSC_TRUE); CHKERRQ(ierr);
    ierr = KSPGetPC(_kspTrans,&_pc); CHKERRQ(ierr);
    ierr = KSPSetTolerances(_kspTrans,_kspTol,_kspTol,PETSC_DEFAULT,PETSC_DEFAULT); CHKERRQ(ierr);
    ierr = PCSetType(_pc,PCHYPRE); CHKERRQ(ierr);
    ierr = PCFactorSetShiftType(_pc,MAT_SHIFT_POSITIVE_DEFINITE); CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(_kspTrans,PETSC_TRUE); CHKERRQ(ierr); CHKERRQ(ierr);
  }
  else {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"ERROR: linSolver type not understood\n");
    assert(0);
  }

    // accept command line options
    ierr = KSPSetFromOptions(_kspTrans);CHKERRQ(ierr);

  // perform computation of preconditioners now, rather than on first use
  double startTime = MPI_Wtime();
  ierr = KSPSetUp(_kspTrans);CHKERRQ(ierr);
  _factorTime += MPI_Wtime() - startTime;

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// explicit time stepping for MMS
PetscErrorCode HeatEquation::d_dt(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::d_dt";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

    //~ d_dt(const PetscScalar time,const Vec slipVel,const Vec& tau,const Vec& sigmaxy,
      //~ const Vec& sigmaxz, const Vec& dgxy, const Vec& dgxz,const Vec& T, Vec& dTdt)

    //~ Vec slipVel; VecDuplicate(_bcL,&slipVel); VecSet(slipVel,0.0);
    //~ Vec tau; VecDuplicate(_bcL,&tau); VecSet(tau,0.0);
    //~ ierr = d_dt(time,slipVel,tau,NULL,NULL,NULL,NULL,*varBegin,*dvarBegin);

    //~ ierr = d_dt_mms(time,*varBegin,*dvarBegin); CHKERRQ(ierr);

    //~ mapToVec(_dT,zzmms_he1_T,*_y,*_z,time);
    //~ VecCopy(*varBegin,_dT);
    //~ mapToVec(*dvarBegin,zzmms_he1_T_t,*_y,*_z,time);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


PetscErrorCode HeatEquation::initiateIntegrand(const PetscScalar time,map<string,Vec>& varEx,map<string,Vec>& varIm)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::initiateIntegrand";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  // put variables to be integrated implicity into varIm
  Vec T;
  VecDuplicate(_Tamb,&T);
  VecWAXPY(T,1.0,_Tamb,_dT);
  varIm["Temp"] = T;


  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode HeatEquation::updateFields(const PetscScalar time,const map<string,Vec>& varEx,const map<string,Vec>& varIm)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::updateFields()";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  //~ Vec T;
  //~ VecCopy(varIm.find("Temp")->second,_dT);
  // not needed for implicit solve

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode HeatEquation::setMMSBoundaryConditions(const double time,
  std::string bcRType,std::string bcTType,std::string bcLType,std::string bcBType)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::setMMSBoundaryConditions";
    string fileName = "heatequation.cpp";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),fileName.c_str());CHKERRQ(ierr);
  #endif

  // set up boundary conditions: L and R
  PetscScalar y,z,v;
  PetscInt Ii,Istart,Iend;
  ierr = VecGetOwnershipRange(_bcL,&Istart,&Iend);CHKERRQ(ierr);
  for(Ii=Istart;Ii<Iend;Ii++) {
    ierr = VecGetValues(*_z,1,&Ii,&z);CHKERRQ(ierr);
    y = 0;
    if (!bcLType.compare("Dirichlet")) { v = zzmms_T(y,z,time); }
    else if (!bcLType.compare("Neumann")) { v = zzmms_k(y,z)*zzmms_T_y(y,z,time); }
    ierr = VecSetValues(_bcL,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);

    y = _Ly;
    if (!bcRType.compare("Dirichlet")) { v = zzmms_T(y,z,time); }
    else if (!bcRType.compare("Neumann")) { v = zzmms_k(y,z)*zzmms_T_y(y,z,time); }
    ierr = VecSetValues(_bcR,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(_bcL);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_bcR);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcL);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcR);CHKERRQ(ierr);

  // set up boundary conditions: T and B
  ierr = VecGetOwnershipRange(*_y,&Istart,&Iend);CHKERRQ(ierr);
  for(Ii=Istart;Ii<Iend;Ii++) {
    if (Ii % _Nz == 0) {
      ierr = VecGetValues(*_y,1,&Ii,&y);CHKERRQ(ierr);
      PetscInt Jj = Ii / _Nz;

      z = 0;
      if (!bcTType.compare("Dirichlet")) { v = zzmms_T(y,z,time); }
      else if (!bcTType.compare("Neumann")) { v = zzmms_k(y,z)*zzmms_T_z(y,z,time); }
      ierr = VecSetValues(_bcT,1,&Jj,&v,INSERT_VALUES);CHKERRQ(ierr);

      z = _Lz;
      if (!bcBType.compare("Dirichlet")) { v = zzmms_T(y,z,time); }
      else if (!bcBType.compare("Neumann")) { v = zzmms_k(y,z)*zzmms_T_z(y,z,time); }
      ierr = VecSetValues(_bcB,1,&Jj,&v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(_bcT);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_bcB);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcT);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcB);CHKERRQ(ierr);

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s.\n",funcName.c_str(),fileName.c_str());
  #endif
  return ierr;
}

PetscErrorCode HeatEquation::measureMMSError(const PetscScalar time)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::setMMSBoundaryConditions";
    string fileName = "heatequation.cpp";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),fileName.c_str());CHKERRQ(ierr);
  #endif

  // measure error between analytical and numerical solution
  Vec dTA;
  VecDuplicate(_dT,&dTA);

  //~ if (_Nz == 1) { mapToVec(dTA,zzmms_uA1D,*_y,time); }
  //~ else { mapToVec(dTA,zzmms_dT,*_y,*_z,time); }
  //~ mapToVec(dTA,zzmms_dT,*_y,*_z,time);
  mapToVec(dTA,zzmms_T,*_y,*_z,time);

  writeVec(dTA,_outputDir+"mms_dTA");
  writeVec(_dT,_outputDir+"mms_dT");

  writeVec(_bcL,_outputDir+"mms_he_bcL");
  writeVec(_bcR,_outputDir+"mms_he_bcR");
  writeVec(_bcT,_outputDir+"mms_he_bcT");
  writeVec(_bcB,_outputDir+"mms_he_bcB");

  double err2u = computeNormDiff_2(_dT,dTA);

  PetscPrintf(PETSC_COMM_WORLD,"%i %3i %.4e %.4e % .15e\n",
              _order,_Ny,_dy,err2u,log2(err2u));

  VecDestroy(&dTA);
  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s.\n",funcName.c_str(),fileName.c_str());
  #endif
  return ierr;
}


// for thermomechanical coupling with explicit time stepping
PetscErrorCode HeatEquation::d_dt(const PetscScalar time,const Vec slipVel,const Vec& tau,const Vec& sigmaxy,
      const Vec& sigmaxz, const Vec& dgxy, const Vec& dgxz,const Vec& T, Vec& dTdt)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::d_dt";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  VecCopy(T,_dT); // so that the correct temperature is written out

  // left boundary: heat generated by fault motion
  Vec vel;
  VecDuplicate(_bcL,&vel);
  VecCopy(slipVel,vel);
  VecPointwiseMult(_bcL,tau,vel);
  VecDestroy(&vel);


  Mat A;
  _sbp->getA(A);
  ierr = MatMult(A,T,dTdt); CHKERRQ(ierr);
  Vec rhs;
  VecDuplicate(T,&rhs);
  ierr = _sbp->setRhs(rhs,_bcL,_bcR,_bcT,_bcB);CHKERRQ(ierr);
  ierr = VecAXPY(dTdt,-1.0,rhs);CHKERRQ(ierr);
  VecDestroy(&rhs);

  Vec temp;
  VecDuplicate(dTdt,&temp);
  _sbp->Hinv(dTdt,temp);
  VecCopy(temp,dTdt);
  VecDestroy(&temp);

  if (dgxy!=NULL && dgxz!=NULL) {
  // shear heating terms: simgaxy*dgxy + sigmaxz*dgxz (stresses times viscous strain rates)
  Vec shearHeat;
  VecDuplicate(sigmaxy,&shearHeat);
  VecSet(shearHeat,0.0);
  VecPointwiseMult(shearHeat,sigmaxy,dgxy);
  VecAXPY(dTdt,1.0,shearHeat);
  if (_Nz > 1) {
    VecSet(shearHeat,0.0);
    VecPointwiseMult(shearHeat,sigmaxz,dgxz);
    VecAXPY(dTdt,1.0,shearHeat);
  }
  VecDestroy(&shearHeat);
  }

  //~//!!! missing h*c term (heat production)

  VecPointwiseDivide(dTdt,dTdt,_rho);
  VecPointwiseDivide(dTdt,dTdt,_c);

  //~ VecSet(dTdt,0.0);
  mapToVec(dTdt,zzmms_T_t,*_y,*_z,time);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}



// MMS test for thermomechanical coupling with explicity time stepping
PetscErrorCode HeatEquation::d_dt_mms(const PetscScalar time,const Vec& T, Vec& dTdt)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::d_dt_mms";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  VecCopy(T,_dT); // so that the correct temperature is written out

  // update boundary conditions
  ierr = setMMSBoundaryConditions(time,"Dirichlet","Dirichlet","Neumann","Dirichlet"); CHKERRQ(ierr);


  Mat A;
  _sbp->getA(A);
  ierr = MatMult(A,T,dTdt); CHKERRQ(ierr);
  Vec rhs;
  VecDuplicate(T,&rhs);
  ierr = _sbp->setRhs(rhs,_bcL,_bcR,_bcT,_bcB);CHKERRQ(ierr);
  ierr = VecAXPY(dTdt,-1.0,rhs);CHKERRQ(ierr);
  VecDestroy(&rhs);

  Vec temp;
  VecDuplicate(dTdt,&temp);
  _sbp->Hinv(dTdt,temp);
  VecCopy(temp,dTdt);
  VecDestroy(&temp);

  VecPointwiseDivide(dTdt,dTdt,_rho);
  VecPointwiseDivide(dTdt,dTdt,_c);


  //~ VecSet(dTdt,0.0);
  //~ mapToVec(dTdt,zzmms_he1_T_t,*_y,*_z,time);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


// for thermomechanical coupling using backward Euler (implicit time stepping)
PetscErrorCode HeatEquation::be(const PetscScalar time,const Vec slipVel,const Vec& tau,
  const Vec& sigmadev, const Vec& dgxy,const Vec& dgxz,Vec& T,const Vec& To,const PetscScalar dt)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::be";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  double beStartTime = MPI_Wtime();

  if (_isMMS && _heatEquationType.compare("transient")==0) {
    assert(0);
    //~ be_transient(time,slipVel,tau,sigmadev,dgxy,dgxz,T,To,dt);
  }
  else if (_isMMS && _heatEquationType.compare("steadyState")==0) {
    be_steadyStateMMS(time,slipVel,tau,sigmadev,dgxy,dgxz,T,To,dt);
  }
  else if (!_isMMS && _heatEquationType.compare("transient")==0) {
    be_transient(time,slipVel,tau,sigmadev,dgxy,dgxz,T,To,dt);
  }
  else if (!_isMMS && _heatEquationType.compare("steadyState")==0) {
    be_steadyState(time,slipVel,tau,sigmadev,dgxy,dgxz,T,To,dt);
  }

  computeHeatFlux();

  _beTime += MPI_Wtime() - beStartTime;

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// for thermomechanical problem using implicit time stepping (backward Euler)
PetscErrorCode HeatEquation::be_transient(const PetscScalar time,const Vec slipVel,const Vec& tau,
  const Vec& sdev, const Vec& dgxy,const Vec& dgxz,Vec& T,const Vec& Tn,const PetscScalar dt)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::be_transient";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  // set up matrix
  MatCopy(_D2ath,_B,SAME_NONZERO_PATTERN);
  MatScale(_B,-dt);
  MatAXPY(_B,1.0,_I,SUBSET_NONZERO_PATTERN);
  if (_kspTrans == NULL) {
    KSPDestroy(&_kspSS);
    setupKSP(_B);
  }
  ierr = KSPSetOperators(_kspTrans,_B,_B);CHKERRQ(ierr);

  // set up boundary conditions and source terms
  Vec rhs,temp;
  VecDuplicate(_dT,&rhs);
  VecDuplicate(_dT,&temp);
  VecSet(rhs,0.0);
  VecSet(temp,0.0);
  if (_wRadioHeatGen.compare("yes") == 0) { VecCopy(_Qrad,_Q); }
  else { VecSet(_Q,0.); }

  // left boundary: heat generated by fault motion
  if (_wFrictionalHeating.compare("yes")==0) {
    // set bcL and/or omega depending on shear zone width
    computeFrictionalShearHeating(tau,slipVel);
    VecAXPY(_Q,1.0,_omega);
  }

  // compute shear heating component
  if (_wViscShearHeating.compare("yes")==0 && dgxy!=NULL && dgxz!=NULL && sdev!=NULL) {
    Vec Qvisc;
    computeViscousShearHeating(Qvisc,sdev, dgxy, dgxz);
    VecAXPY(_Q,1.0,Qvisc);
    VecDestroy(&Qvisc);
  }


  ierr = _sbp->setRhs(temp,_bcL,_bcR,_bcT,_bcB);CHKERRQ(ierr);
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Vec temp1; VecDuplicate(_Q,&temp1);
    Mat J,Jinv,qy,rz,yq,zr;
    ierr = _sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr); CHKERRQ(ierr);
    ierr = MatMult(J,_Q,temp1);
    Mat H; _sbp->getH(H);
    ierr = MatMult(H,temp1,temp); CHKERRQ(ierr);
    VecDestroy(&temp1);
  }
  else {
    Mat H; _sbp->getH(H);
    ierr = MatMult(H,_Q,temp); CHKERRQ(ierr);
  }
  MatMultAdd(_rcInv,temp,rhs,rhs);
  VecScale(rhs,dt);


  // add H * Tn to rhs
  VecSet(temp,0.0);
  _sbp->H(Tn,temp);
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Mat J,Jinv,qy,rz,yq,zr;
    ierr = _sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr); CHKERRQ(ierr);
    Vec temp1; VecDuplicate(temp,&temp1);
    MatMult(J,temp,temp1);
    VecCopy(temp1,temp);
    VecDestroy(&temp1);
  }
  VecAXPY(rhs,1.0,temp);
  VecDestroy(&temp);

  // solve for temperature and record run time required
  VecCopy(Tn,_T);
  double startTime = MPI_Wtime();
  KSPSolve(_kspTrans,rhs,_T);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;
  VecDestroy(&rhs);

  VecCopy(_T,T);
  VecWAXPY(_dT,-1.0,_Tamb,_T); // dT = T - Tamb

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// for thermomechanical problem only the steady-state heat equation
PetscErrorCode HeatEquation::be_steadyState(const PetscScalar time,const Vec slipVel,const Vec& tau,
  const Vec& sdev, const Vec& dgxy,const Vec& dgxz,Vec& T,const Vec& Tn,const PetscScalar dt)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::be_steadyState";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  if (_kspSS == NULL) {
    KSPDestroy(&_kspTrans);
    Mat A; _sbp->getA(A);
    setupKSP_SS(A);
  }

  // set up boundary conditions and source terms
  Vec rhs; VecDuplicate(_dT,&rhs); VecSet(rhs,0.0);
  if (_wRadioHeatGen.compare("yes") == 0) { VecCopy(_Qrad,_Q); VecScale(_Q,-1); }
  else { VecSet(_Q,0.); }

  // left boundary: heat generated by fault motion
  if (_wFrictionalHeating.compare("yes")==0) {
    // set bcL and/or omega depending on shear zone width
    computeFrictionalShearHeating(tau,slipVel);
    VecAXPY(_Q,-1.0,_omega);
    VecScale(_bcL,-1.);
  }

  // compute shear heating component
  if (_wViscShearHeating.compare("yes")==0 && dgxy!=NULL && dgxz!=NULL && sdev!=NULL) {
    Vec Qvisc;
    computeViscousShearHeating(Qvisc,sdev, dgxy, dgxz);
    VecAXPY(_Q,-1.0,Qvisc);
    VecDestroy(&Qvisc);
  }

  // rhs = J*H*Q + (SAT BC terms)
  ierr = _sbp->setRhs(rhs,_bcL,_bcR,_bcT,_bcB);CHKERRQ(ierr);
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Vec temp1; VecDuplicate(_Q,&temp1);
    Mat J,Jinv,qy,rz,yq,zr;
    ierr = _sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr); CHKERRQ(ierr);
    ierr = MatMult(J,_Q,temp1);
    Mat H; _sbp->getH(H);
    ierr = MatMultAdd(H,temp1,rhs,rhs);
    VecDestroy(&temp1);
  }
  else{
    Mat H; _sbp->getH(H);
    ierr = MatMultAdd(H,_Q,rhs,rhs); CHKERRQ(ierr);
  }

  // solve for temperature and record run time required
  VecCopy(Tn,_T);
  double startTime = MPI_Wtime();
  KSPSolve(_kspSS,rhs,_T);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;

  VecCopy(_T,T);
  VecWAXPY(_dT,-1.0,_Tamb,_T); // dT = - Tamb + T
  computeHeatFlux();

  VecDestroy(&rhs);
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// for thermomechanical coupling solving only the steady-state heat equation with MMS test
PetscErrorCode HeatEquation::be_steadyStateMMS(const PetscScalar time,const Vec slipVel,const Vec& tau,
  const Vec& sigmadev, const Vec& dgxy,const Vec& dgxz,Vec& T,const Vec& To,const PetscScalar dt)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::be_steadyStateMMS";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
/*
  // set up boundary conditions and source terms
  Vec rhs,temp;
  VecDuplicate(_dT,&rhs);
  VecDuplicate(_dT,&temp);
  VecSet(rhs,0.0);
  VecSet(temp,0.0);


  setMMSBoundaryConditions(time,"Dirichlet","Dirichlet","Neumann","Dirichlet");
  ierr = _sbp->setRhs(rhs,_bcL,_bcR,_bcT,_bcB);CHKERRQ(ierr);
  Vec source,Hxsource;
  VecDuplicate(_dT,&source);
  VecDuplicate(_dT,&Hxsource);
  //~ mapToVec(source,zzmms_SSdTsource,*_y,*_z,time);
  mapToVec(source,zzmms_SSTsource,*_y,*_z,time);
  ierr = _sbp->H(source,Hxsource);
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Mat J,Jinv,qy,rz,yq,zr;
    ierr = _sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr); CHKERRQ(ierr);
    multMatsVec(yq,zr,Hxsource);
  }
  //~ writeVec(source,_outputDir+"mms_SSdTsource");
  VecDestroy(&source);
  ierr = VecAXPY(rhs,1.0,Hxsource);CHKERRQ(ierr); // rhs = rhs + H*source
  VecDestroy(&Hxsource);


  //~ // compute shear heating component
  //~ if (_wViscShearHeating.compare("yes")==0 && dgxy!=NULL && dgxz!=NULL) {
    //~ Vec shearHeat;
    //~ computeViscousShearHeating(shearHeat,sigmadev, dgxy, dgxz);
    //~ VecSet(shearHeat,0.);
    //~ VecAXPY(temp,1.0,shearHeat);
    //~ VecDestroy(&shearHeat);
  //~ }

  // solve for temperature and record run time required
  double startTime = MPI_Wtime();
  //~ VecCopy(To,_dT); // plausible guess
  KSPSolve(_ksp,rhs,_dT);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;
  VecCopy(_dT,T);
  //~ VecWAXPY(T,1.0,_dT,_Tamb); // T = dT + T0

  //~ mapToVec(_dT,zzmms_T,*_y,*_z,time);

  VecDestroy(&rhs);
  * */
  assert(0);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

PetscErrorCode HeatEquation::initiateVarSS(map<string,Vec>& varSS)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::initiateVarSS";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  // put variables to be integrated implicity into varIm
  Vec T;
  VecDuplicate(_Tamb,&T);
  VecWAXPY(T,1.0,_Tamb,_dT);
  varSS["Temp"] = T;

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode HeatEquation::updateSS(map<string,Vec>& varSS)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::updateSS";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  Vec slipVel = varSS.find("slipVel")->second;
  Vec tau = varSS.find("tau")->second;
  Vec sDev = varSS.find("sDev")->second;
  Vec gVxy_t = varSS.find("gVxy_t")->second;
  Vec gVxz_t = varSS.find("gVxz_t")->second;

  // final argument is output
  ierr = computeSteadyStateTemp(0,slipVel,tau,sDev,gVxy_t,gVxz_t,varSS["Temp"]); CHKERRQ(ierr);

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// compute steady-state temperature given boundary conditions and shear heating source terms (assuming these remain constant)
PetscErrorCode HeatEquation::computeSteadyStateTemp(const PetscScalar time,const Vec slipVel,const Vec& tau,
  const Vec& sigmadev, const Vec& dgxy,const Vec& dgxz,Vec& T)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::computeSteadyStateTemp";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  double beStartTime = MPI_Wtime();

  if (_kspSS == NULL) {
    KSPDestroy(&_kspTrans);
    Mat A; _sbp->getA(A);
    setupKSP_SS(A);
  }

  // set up boundary conditions and source terms
  Vec rhs; VecDuplicate(_dT,&rhs); VecSet(rhs,0.0);
  if (_wRadioHeatGen.compare("yes") == 0) { VecCopy(_Qrad,_Q); VecScale(_Q,-1); }
  else { VecSet(_Q,0.); }

  // left boundary: heat generated by fault motion
  if (_wFrictionalHeating.compare("yes")==0) {
    // set bcL and/or omega depending on shear zone width
    computeFrictionalShearHeating(tau,slipVel);
    VecAXPY(_Q,-1.0,_omega);
    VecScale(_bcL,-1.);
  }

  // compute shear heating component
  if (_wViscShearHeating.compare("yes")==0 && dgxy!=NULL && dgxz!=NULL && sigmadev!=NULL) {
    Vec Qvisc;
    computeViscousShearHeating(Qvisc,sigmadev, dgxy, dgxz);
    VecAXPY(_Q,-1.0,Qvisc);
    VecDestroy(&Qvisc);
  }

  // rhs = J*H*Q + (SAT BC terms)
  ierr = _sbp->setRhs(rhs,_bcL,_bcR,_bcT,_bcB);CHKERRQ(ierr);
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Vec temp1; VecDuplicate(_Q,&temp1);
    Mat J,Jinv,qy,rz,yq,zr;
    ierr = _sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr); CHKERRQ(ierr);
    ierr = MatMult(J,_Q,temp1);
    Mat H; _sbp->getH(H);
    ierr = MatMultAdd(H,temp1,rhs,rhs);
    VecDestroy(&temp1);
  }
  else{
    Mat H; _sbp->getH(H);
    ierr = MatMultAdd(H,_Q,rhs,rhs); CHKERRQ(ierr);
  }

  // solve for temperature and record run time required
  double startTime = MPI_Wtime();
  KSPSolve(_kspSS,rhs,_T);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;

  VecWAXPY(_dT,-1.0,_Tamb,_T); // dT = - Tamb + T
  VecCopy(_T,T);


  computeHeatFlux();

  VecDestroy(&rhs);
  _beTime += MPI_Wtime() - beStartTime;

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


// compute viscous shear heating term (uses temperature from previous time step)
PetscErrorCode HeatEquation::computeViscousShearHeating(Vec& Qvisc,const Vec& sigmadev, const Vec& dgxy, const Vec& dgxz)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::computeViscousShearHeating";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  // shear heating terms: sigmadev * dgv  (stresses times viscous strain rates)
  // sigmadev = sqrt(sxy^2 + sxz^2)
  // dgv = sqrt(dgVxy^2 + dgVxz^2)
  VecDuplicate(sigmadev,&Qvisc);
  VecSet(Qvisc,0.0);


  // compute dgv (use shearHeat to store values)
  VecPointwiseMult(Qvisc,dgxy,dgxy);
  Vec temp;
  VecDuplicate(sigmadev,&temp);
  VecPointwiseMult(temp,dgxz,dgxz);
  VecAXPY(Qvisc,1.0,temp);
  VecDestroy(&temp);
  VecSqrtAbs(Qvisc);

  // multiply by deviatoric stress
  VecPointwiseMult(Qvisc,sigmadev,Qvisc);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// compute frictional shear heating term (uses temperature from previous time step)
PetscErrorCode HeatEquation::computeFrictionalShearHeating(const Vec& tau, const Vec& slipVel)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::computeFrictionalShearHeating";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  // if left boundary condition is heat flux
  if (_wMax == 0) {
    VecSet(_bcL,0.);
    VecPointwiseMult(_bcL,tau,slipVel);
    VecScale(_bcL,0.5);
    VecSet(_omega,0.);
  }

  // if using finite width shear zone
  else {
    VecSet(_bcL,0.); // q = 0, no flux
    Vec V,Tau;
    VecDuplicate(_Gw,&V);
    VecDuplicate(_Gw,&Tau);
    ierr = MatMult(_MapV,slipVel,V); CHKERRQ(ierr);
    ierr = MatMult(_MapV,tau,Tau); CHKERRQ(ierr);
    VecPointwiseMult(_omega,V,_Gw);
    VecPointwiseMult(_omega,Tau,_omega);
    VecDestroy(&V);
    VecDestroy(&Tau);
  }


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// set up KSP, matrices, boundary conditions for the steady state heat equation problem
PetscErrorCode HeatEquation::setUpSteadyStateProblem()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::setUpSteadyStateProblem";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  delete _sbp; _sbp = NULL;
  setBCsforBE();

  std::string bcRType = "Dirichlet";
  std::string bcTType = "Dirichlet";
  std::string bcLType = "Neumann";
  std::string bcBType = "Dirichlet";

  delete _sbp;

  // construct matrices
  if (_sbpType.compare("mc")==0) {
    _sbp = new SbpOps_c(_order,_Ny,_Nz,_Ly,_Lz,_k);
  }
  else if (_sbpType.compare("mfc")==0) {
    _sbp = new SbpOps_fc(_order,_Ny,_Nz,_Ly,_Lz,_k);
  }
  else if (_sbpType.compare("mfc_coordTrans")==0) {
    _sbp = new SbpOps_fc_coordTrans(_order,_Ny,_Nz,_Ly,_Lz,_k);
    if (_Ny > 1 && _Nz > 1) { _sbp->setGrid(_y,_z); }
    else if (_Ny == 1 && _Nz > 1) { _sbp->setGrid(NULL,_z); }
    else if (_Ny > 1 && _Nz == 1) { _sbp->setGrid(_y,NULL); }
  }
  else {
    PetscPrintf(PETSC_COMM_WORLD,"ERROR: SBP type type not understood\n");
    assert(0); // automatically fail
  }
  _sbp->setBCTypes(bcRType,bcTType,bcLType,bcBType);
  _sbp->setMultiplyByH(1);
  _sbp->setLaplaceType("yz");
  _sbp->setDeleteIntermediateFields(1);
  _sbp->computeMatrices(); // actually create the matrices


#if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// set up KSP, matrices, boundary conditions for the transient heat equation problem
PetscErrorCode HeatEquation::setUpTransientProblem()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::setUpTransientProblem";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  // update boundaries (for solving for perturbation from steady-state)
  setBCsforBE();

  delete _sbp; _sbp = NULL;
  // construct matrices
  // BC order: right,top, left, bottom
  if (_sbpType.compare("mc")==0) {
    _sbp = new SbpOps_c(_order,_Ny,_Nz,_Ly,_Lz,_k);
  }
  else if (_sbpType.compare("mfc")==0) {
    _sbp = new SbpOps_fc(_order,_Ny,_Nz,_Ly,_Lz,_k);
  }
  else if (_sbpType.compare("mfc_coordTrans")==0) {
    _sbp = new SbpOps_fc_coordTrans(_order,_Ny,_Nz,_Ly,_Lz,_k);
    if (_Ny > 1 && _Nz > 1) { _sbp->setGrid(_y,_z); }
    else if (_Ny == 1 && _Nz > 1) { _sbp->setGrid(NULL,_z); }
    else if (_Ny > 1 && _Nz == 1) { _sbp->setGrid(_y,NULL); }
  }
  else {
    PetscPrintf(PETSC_COMM_WORLD,"ERROR: SBP type type not understood\n");
    assert(0); // automatically fail
  }
  _sbp->setBCTypes("Dirichlet","Dirichlet","Neumann","Dirichlet");
  _sbp->setMultiplyByH(1);
  _sbp->setLaplaceType("yz");
  _sbp->computeMatrices(); // actually create the matrices

  // create identity matrix I (multiplied by H)
  Mat H;
  _sbp->getH(H);
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Mat J,Jinv,qy,rz,yq,zr;
    ierr = _sbp->getCoordTrans(J,Jinv,qy,rz,yq,zr); CHKERRQ(ierr);
    MatMatMult(J,H,MAT_INITIAL_MATRIX,PETSC_DEFAULT,&_I);
  }
  else {
    MatDuplicate(H,MAT_COPY_VALUES,&_I);
  }

  // create (rho*c)^-1 vector and matrix
  Vec rhocV;
  VecDuplicate(_rho,&rhocV);
  VecSet(rhocV,1.);
  VecPointwiseDivide(rhocV,rhocV,_rho);
  VecPointwiseDivide(rhocV,rhocV,_c);
  MatDuplicate(_I,MAT_DO_NOT_COPY_VALUES,&_rcInv);
  MatDiagonalSet(_rcInv,rhocV,INSERT_VALUES);

  // create _D2ath = (rho*c)^-1 H D2
  Mat D2;
  _sbp->getA(D2);
  MatMatMult(_rcInv,D2,MAT_INITIAL_MATRIX,PETSC_DEFAULT,&_D2ath);

  MatDuplicate(_D2ath,MAT_COPY_VALUES,&_B);

  // ensure diagonal of _D2ath has been allocated, even if 0
  PetscScalar v=0.0;
  PetscInt Ii,Istart,Iend=0;
  MatGetOwnershipRange(_D2ath,&Istart,&Iend);
  for (Ii = Istart; Ii < Iend; Ii++) {
    MatSetValues(_D2ath,1,&Ii,1,&Ii,&v,ADD_VALUES);
  }
  MatAssemblyBegin(_D2ath,MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(_D2ath,MAT_FINAL_ASSEMBLY);

  VecDestroy(&rhocV);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// set right boundary condition from computed geotherm
PetscErrorCode HeatEquation::setBCsforBE()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::setBCs";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  //~ // only solve for dT with linear solve
  //~ VecSet(_bcR,0.0);
  //~ VecSet(_bcT,0.0);
  //~ VecSet(_bcB,0.0);
  //~ VecSet(_bcL,0.0);

  // set up boundary conditions
  VecScatterBegin(_D->_scatters["body2T"], _Tamb, _bcT, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd(_D->_scatters["body2T"], _Tamb, _bcT, INSERT_VALUES, SCATTER_FORWARD);

  VecScatterBegin(_D->_scatters["body2B"], _Tamb, _bcB, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd(_D->_scatters["body2B"], _Tamb, _bcB, INSERT_VALUES, SCATTER_FORWARD);

  VecScatterBegin(_D->_scatters["body2R"], _Tamb, _bcR, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd(_D->_scatters["body2R"], _Tamb, _bcR, INSERT_VALUES, SCATTER_FORWARD);

  VecSet(_bcL,0);

#if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


// compute heat flux (full body field and surface heat flux) for output
PetscErrorCode HeatEquation::computeHeatFlux()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::computeHeatFlux";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif


  // total heat flux
  ierr = _sbp->muxDz(_T,_kTz); CHKERRQ(ierr);
  VecScale(_kTz,1e9);

  // extract surface heat flux
  VecScatterBegin(_D->_scatters["body2T"], _kTz, _kTz_z0, INSERT_VALUES, SCATTER_FORWARD);
  VecScatterEnd(_D->_scatters["body2T"], _kTz, _kTz_z0, INSERT_VALUES, SCATTER_FORWARD);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


PetscErrorCode HeatEquation::writeStep1D(const PetscInt stepCount, const PetscScalar time,const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::writeStep1D";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s at step %i\n",funcName.c_str(),FILENAME,stepCount);
    CHKERRQ(ierr);
  #endif

  double startTime = MPI_Wtime();

  if (stepCount == 0) {
    ierr = io_initiateWriteAppend(_viewers, "kTz_y0", _kTz_z0, outputDir + "he_kTz_y0"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "he_bcR", _bcR, outputDir + "he_bcR"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "he_bcT", _bcT, outputDir + "he_bcT"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "he_bcB", _bcB, outputDir + "he_bcB"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "he_bcL", _bcL, outputDir + "he_bcL"); CHKERRQ(ierr);
  }
  else {
    ierr = VecView(_kTz_z0,_viewers["kTz_y0"].first); CHKERRQ(ierr);
    ierr = VecView(_bcR,_viewers["he_bcR"].first); CHKERRQ(ierr);
    ierr = VecView(_bcT,_viewers["he_bcT"].first); CHKERRQ(ierr);
    ierr = VecView(_bcL,_viewers["he_bcL"].first); CHKERRQ(ierr);
    ierr = VecView(_bcB,_viewers["he_bcB"].first); CHKERRQ(ierr);
  }

  _writeTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s at step %i\n",funcName.c_str(),FILENAME,stepCount);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

PetscErrorCode HeatEquation::writeStep2D(const PetscInt stepCount, const PetscScalar time,const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::writeStep2D";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s at step %i\n",funcName.c_str(),FILENAME,stepCount);
    CHKERRQ(ierr);
  #endif

  double startTime = MPI_Wtime();

  if (stepCount == 0) {
    ierr = io_initiateWriteAppend(_viewers, "T", _T, outputDir + "he_T"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "dT", _dT, outputDir + "he_dT"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "kTz", _kTz, outputDir + "he_kTz"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "omega", _omega, outputDir + "he_omega"); CHKERRQ(ierr);
    ierr = io_initiateWriteAppend(_viewers, "Q", _Q, outputDir + "he_Q"); CHKERRQ(ierr);
  }
  else {
    ierr = VecView(_T,_viewers["T"].first); CHKERRQ(ierr);
    ierr = VecView(_dT,_viewers["dT"].first); CHKERRQ(ierr);
    ierr = VecView(_kTz,_viewers["kTz"].first); CHKERRQ(ierr);
    ierr = VecView(_omega,_viewers["omega"].first); CHKERRQ(ierr);
    ierr = VecView(_Q,_viewers["Q"].first); CHKERRQ(ierr);
  }

  _writeTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s at step %i\n",funcName.c_str(),FILENAME,stepCount);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

PetscErrorCode HeatEquation::view()
{
  PetscErrorCode ierr = 0;
  //~ ierr = _quadEx->view();
  ierr = PetscPrintf(PETSC_COMM_WORLD,"-------------------------------\n\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Heat Equation Runtime Summary:\n");CHKERRQ(ierr);
  //~ ierr = PetscPrintf(PETSC_COMM_WORLD,"   solver algorithm = %s\n",_linSolver.c_str());CHKERRQ(ierr);
  //~ ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent setting up linear solve context (e.g. factoring) (s): %g\n",_factorTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent in be (s): %g\n",_beTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent writing output (s): %g\n",_writeTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   number of times linear system was solved: %i\n",_linSolveCount);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent solving linear system (s): %g\n",_linSolveTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   %% be time spent solving linear system: %g\n",_linSolveTime/_beTime*100.);CHKERRQ(ierr);

  //~ ierr = PetscPrintf(PETSC_COMM_WORLD,"   misc time (s): %g\n",_miscTime);CHKERRQ(ierr);
  //~ ierr = PetscPrintf(PETSC_COMM_WORLD,"   %% misc time: %g\n",_miscTime/_beTime*100.);CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"\n");CHKERRQ(ierr);

  return ierr;
}

// Save all scalar fields to text file named he_domain.txt in output directory.
// Note that only the rank 0 processor's values will be saved.
PetscErrorCode HeatEquation::writeDomain(const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::writeDomain";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  // output scalar fields
  std::string str = outputDir + "he_context.txt";
  PetscViewer    viewer;

  PetscViewerCreate(PETSC_COMM_WORLD, &viewer);
  PetscViewerSetType(viewer, PETSCVIEWERASCII);
  PetscViewerFileSetMode(viewer, FILE_MODE_WRITE);
  PetscViewerFileSetName(viewer, str.c_str());

  ierr = PetscViewerASCIIPrintf(viewer,"heatEquationType = %s\n",_heatEquationType.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"withViscShearHeating = %s\n",_wViscShearHeating.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"withFrictionalHeating = %s\n",_wFrictionalHeating.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"withRadioHeatGeneration = %s\n",_wRadioHeatGen.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"linSolver_heateq = %s\n",_linSolver.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"sbpType_heateq = %s\n",_sbpType.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"kspTol_heateq = %.15e\n",_kspTol);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"\n");CHKERRQ(ierr);

  ierr = PetscViewerASCIIPrintf(viewer,"TVals = %s\n",vector2str(_TVals).c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"TDepths = %s\n",vector2str(_TDepths).c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"w = %.5e\n",_w);CHKERRQ(ierr);


  PetscMPIInt size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  ierr = PetscViewerASCIIPrintf(viewer,"numProcessors = %i\n",size);CHKERRQ(ierr);

  PetscViewerDestroy(&viewer);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// write out material properties
PetscErrorCode HeatEquation::writeContext(const std::string outputDir)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "HeatEquation::writeContext";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  writeDomain(outputDir);

  ierr = writeVec(_k,outputDir + "he_k"); CHKERRQ(ierr);
  ierr = writeVec(_rho,outputDir + "he_rho"); CHKERRQ(ierr);
  ierr = writeVec(_Qrad,outputDir + "he_h"); CHKERRQ(ierr);
  ierr = writeVec(_c,outputDir + "he_c"); CHKERRQ(ierr);
  ierr = writeVec(_Tamb,outputDir + "he_Tamb"); CHKERRQ(ierr);
  if (_wFrictionalHeating.compare("yes")==0) {
    ierr = writeVec(_Gw,outputDir + "he_Gw"); CHKERRQ(ierr);
    VecScale(_w,1e3); // output w in m
    ierr = writeVec(_w,outputDir + "he_w"); CHKERRQ(ierr);
    VecScale(_w,1e-3); // convert w from m to km
  }

  if (_wRadioHeatGen.compare("yes")==0) {
    ierr = writeVec(_Qrad,outputDir + "he_Qrad"); CHKERRQ(ierr);
  }

  //~ ierr = _sbp->writeOps(_outputDir + "ops_he_"); CHKERRQ(ierr);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}



// Fills vec with the linear interpolation between the pairs of points (vals,depths)
PetscErrorCode HeatEquation::setVecFromVectors(Vec& vec, vector<double>& vals,vector<double>& depths)
{
  PetscErrorCode ierr = 0;
  PetscInt       Istart,Iend;
  PetscScalar    v,z,z0,z1,v0,v1;
  #if VERBOSE > 1
    std::string funcName = "HeatEquation::setVecFromVectors";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  // build structure from generalized input
  size_t vecLen = depths.size();
  ierr = VecGetOwnershipRange(vec,&Istart,&Iend);CHKERRQ(ierr);
  for (PetscInt Ii=Istart;Ii<Iend;Ii++)
  {
    //~ z = _dz*(Ii-_Nz*(Ii/_Nz));
    VecGetValues(*_z,1,&Ii,&z);CHKERRQ(ierr);
    //~PetscPrintf(PETSC_COMM_WORLD,"1: Ii = %i, z = %g\n",Ii,z);
    for (size_t ind = 0; ind < vecLen-1; ind++) {
        z0 = depths[0+ind];
        z1 = depths[0+ind+1];
        v0 = vals[0+ind];
        v1 = vals[0+ind+1];
        if (z>=z0 && z<=z1) { v = (v1 - v0)/(z1-z0) * (z-z0) + v0; }
        ierr = VecSetValues(vec,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(vec);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(vec);CHKERRQ(ierr);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


//======================================================================
// MMS  tests


double HeatEquation::zzmms_rho(const double y,const double z) { return 1.0; }
double HeatEquation::zzmms_c(const double y,const double z) { return 1.0; }
double HeatEquation::zzmms_h(const double y,const double z) { return 0.0; }

//~ double HeatEquation::zzmms_k(const double y,const double z) { return sin(y)*sin(z) + 2.0; }
double HeatEquation::zzmms_k(const double y,const double z) { return sin(y)*sin(z) + 30.; }
double HeatEquation::zzmms_k_y(const double y,const double z) { return cos(y)*sin(z); }
double HeatEquation::zzmms_k_z(const double y,const double z) { return sin(y)*cos(z); }


//~ double HeatEquation::zzmms_f(const double y,const double z) { return cos(y)*sin(z) + 800.; }
double HeatEquation::zzmms_f(const double y,const double z) { return cos(y)*sin(z); }
double HeatEquation::zzmms_f_y(const double y,const double z) { return -sin(y)*sin(z); }
double HeatEquation::zzmms_f_yy(const double y,const double z) { return -cos(y)*sin(z); }
double HeatEquation::zzmms_f_z(const double y,const double z) { return cos(y)*cos(z); }
double HeatEquation::zzmms_f_zz(const double y,const double z) { return -cos(y)*sin(z); }
double HeatEquation::zzmms_g(const double t) { return exp(-2.*t); }
double HeatEquation::zzmms_g_t(const double t) { return -2.*exp(-2.*t); }

double HeatEquation::zzmms_T(const double y,const double z,const double t) { return zzmms_f(y,z)*zzmms_g(t); }
double HeatEquation::zzmms_T_y(const double y,const double z,const double t) { return zzmms_f_y(y,z)*zzmms_g(t); }
double HeatEquation::zzmms_T_yy(const double y,const double z,const double t) { return zzmms_f_yy(y,z)*zzmms_g(t); }
double HeatEquation::zzmms_T_z(const double y,const double z,const double t) { return zzmms_f_z(y,z)*zzmms_g(t); }
double HeatEquation::zzmms_T_zz(const double y,const double z,const double t) { return zzmms_f_zz(y,z)*zzmms_g(t); }
double HeatEquation::zzmms_T_t(const double y,const double z,const double t) { return zzmms_f(y,z)*zzmms_g_t(t); }

double HeatEquation::zzmms_dT(const double y,const double z,const double t) { return zzmms_T(y,z,t) - zzmms_T(y,z,0.); }
double HeatEquation::zzmms_dT_y(const double y,const double z,const double t) { return zzmms_T_y(y,z,t) - zzmms_T_y(y,z,0.); }
double HeatEquation::zzmms_dT_yy(const double y,const double z,const double t) { return zzmms_T_yy(y,z,t) - zzmms_T_yy(y,z,0.); }
double HeatEquation::zzmms_dT_z(const double y,const double z,const double t) { return zzmms_T_z(y,z,t) - zzmms_T_z(y,z,0.); }
double HeatEquation::zzmms_dT_zz(const double y,const double z,const double t) { return zzmms_T_zz(y,z,t) - zzmms_T_zz(y,z,0.); }
double HeatEquation::zzmms_dT_t(const double y,const double z,const double t) { return zzmms_T_t(y,z,t) - zzmms_T_t(y,z,0.); }

double HeatEquation::zzmms_SSTsource(const double y,const double z,const double t)
{
  PetscScalar k = zzmms_k(y,z);
  PetscScalar k_y = zzmms_k_y(y,z);
  PetscScalar k_z = zzmms_k_z(y,z);
  PetscScalar T_y = zzmms_T_y(y,z,t);
  PetscScalar T_yy = zzmms_T_yy(y,z,t);
  PetscScalar T_z = zzmms_T_z(y,z,t);
  PetscScalar T_zz = zzmms_T_zz(y,z,t);
  return k*(T_yy + T_zz) + k_y*T_y + k_z*T_z;
}
double HeatEquation::zzmms_SSdTsource(const double y,const double z,const double t)
{
  PetscScalar k = zzmms_k(y,z);
  PetscScalar k_y = zzmms_k_y(y,z);
  PetscScalar k_z = zzmms_k_z(y,z);
  PetscScalar dT_y = zzmms_dT_y(y,z,t);
  PetscScalar dT_yy = zzmms_dT_yy(y,z,t);
  PetscScalar dT_z = zzmms_dT_z(y,z,t);
  PetscScalar dT_zz = zzmms_dT_zz(y,z,t);
  return k*(dT_yy + dT_zz) + k_y*dT_y + k_z*dT_z;
}







