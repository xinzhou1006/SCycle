#include "powerLaw.hpp"

#define FILENAME "powerLaw.cpp"


PowerLaw::PowerLaw(Domain& D)
: SymmLinearElastic(D), _file(D._file),_delim(D._delim),_inputDir(D._inputDir),
  _viscDistribution("unspecified"),_AFile("unspecified"),_BFile("unspecified"),_nFile("unspecified"),
  _A(NULL),_n(NULL),_B(NULL),_effVisc(NULL),
  _sxzP(NULL),_sigmadev(NULL),
  _gxyP(NULL),_dgxyP(NULL),
  _gxzP(NULL),_dgxzP(NULL),
  _gTxyP(NULL),_gTxzP(NULL),
  //~ _T(NULL),
  _sxyPV(NULL),_sxzPV(NULL),_sigmadevV(NULL),
  _gTxyPV(NULL),_gTxzPV(NULL),
  _gxyPV(NULL),_dgxyPV(NULL),
  _gxzPV(NULL),_dgxzPV(NULL),
  _TV(NULL),_effViscV(NULL)
{
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::PowerLaw";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  loadSettings(_file);
  checkInput();
  allocateFields(); // initialize fields
  setMaterialParameters();

  if (D._loadICs==1) {
    loadFieldsFromFiles();
    setUpSBPContext(D); // set up matrix operators
    setStresses(_initTime);
    computeViscosity();
  }
  else {
    guessSteadyStateEffVisc();
    setSSInitialConds(D);
    setUpSBPContext(D); // set up matrix operators
    setStresses(_initTime);
  }

  // add viscous strain to integrated variables, stored in _var
  Vec vargxyP; VecDuplicate(_uP,&vargxyP); VecCopy(_gxyP,vargxyP);
  Vec vargxzP; VecDuplicate(_uP,&vargxzP); VecCopy(_gxzP,vargxzP);
  //~ _var.push_back(vargxyP);
  //~ _var.push_back(vargxzP);
  _varEx["gVxy"] = vargxyP;
  _varEx["gVxz"] = vargxzP;

  if (_isMMS) { setMMSInitialConditions(); }

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
}

PowerLaw::~PowerLaw()
{
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::~PowerLaw";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  VecDestroy(&_T);
  VecDestroy(&_A);
  VecDestroy(&_n);
  VecDestroy(&_B);
  VecDestroy(&_effVisc);

  VecDestroy(&_sxzP);
  VecDestroy(&_sigmadev);

  VecDestroy(&_gTxyP);
  VecDestroy(&_gTxzP);
  VecDestroy(&_gxyP);
  VecDestroy(&_gxzP);
  VecDestroy(&_dgxyP);
  VecDestroy(&_dgxzP);

  PetscViewerDestroy(&_sxyPV);
  PetscViewerDestroy(&_sxzPV);
  PetscViewerDestroy(&_sigmadevV);
  PetscViewerDestroy(&_gTxyPV);
  PetscViewerDestroy(&_gTxzPV);
  PetscViewerDestroy(&_gxyPV);
  PetscViewerDestroy(&_gxzPV);
  PetscViewerDestroy(&_dgxyPV);
  PetscViewerDestroy(&_dgxzPV);
  PetscViewerDestroy(&_effViscV);

  PetscViewerDestroy(&_timeV2D);

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
}



// loads settings from the input text file
PetscErrorCode PowerLaw::loadSettings(const char *file)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
    std::string funcName = "PowerLaw::loadSettings()";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
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

    // viscosity for asthenosphere
    if (var.compare("viscDistribution")==0) {
      _viscDistribution = line.substr(pos+_delim.length(),line.npos).c_str();
    }

    // names of each field's source file
    else if (var.compare("AFile")==0) {
      _AFile = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("BFile")==0) {
      _BFile = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("nFile")==0) {
      _nFile = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    //~else if (var.compare("TFile")==0) {
      //~_TFile = line.substr(pos+_delim.length(),line.npos).c_str();
    //~}

    // if values are set by a vector
    else if (var.compare("AVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_AVals);
    }
    else if (var.compare("ADepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_ADepths);
    }
    else if (var.compare("BVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_BVals);
    }
    else if (var.compare("BDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_BDepths);
    }
    else if (var.compare("nVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_nVals);
    }
    else if (var.compare("nDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_nDepths);
    }
    else if (var.compare("thermalCoupling")==0) {
      _thermalCoupling = line.substr(pos+_delim.length(),line.npos).c_str();
    }
    else if (var.compare("heatEquationType")==0) {
      _heatEquationType = line.substr(pos+_delim.length(),line.npos).c_str();
    }

  }

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// Check that required fields have been set by the input file
PetscErrorCode PowerLaw::checkInput()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::checkInput";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  assert(_viscDistribution.compare("layered")==0 ||
      _viscDistribution.compare("mms")==0 ||
      _viscDistribution.compare("loadFromFile")==0 ||
      _viscDistribution.compare("effectiveVisc")==0 );

  assert(_heatEquationType.compare("transient")==0 ||
      _heatEquationType.compare("steadyState")==0 );

  assert(_AVals.size() == _ADepths.size() );
  assert(_BVals.size() == _BDepths.size() );
  assert(_nVals.size() == _nDepths.size() );

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// allocate space for member fields
PetscErrorCode PowerLaw::allocateFields()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::allocateFields";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  ierr = VecDuplicate(_uP,&_A);CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&_B);CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&_n);CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&_effVisc);CHKERRQ(ierr);


  // allocate space for stress and strain vectors
  VecDuplicate(_uP,&_sxzP); VecSet(_sxzP,0.0);
  VecDuplicate(_uP,&_sigmadev); VecSet(_sigmadev,0.0);

  VecDuplicate(_uP,&_gxyP);
  PetscObjectSetName((PetscObject) _gxyP, "_gxyP");
  VecSet(_gxyP,0.0);
  VecDuplicate(_uP,&_dgxyP);
  PetscObjectSetName((PetscObject) _dgxyP, "_dgxyP");
  VecSet(_dgxyP,0.0);

  VecDuplicate(_uP,&_gxzP);
  PetscObjectSetName((PetscObject) _gxzP, "_gxzP");
  VecSet(_gxzP,0.0);
  VecDuplicate(_uP,&_dgxzP);
  PetscObjectSetName((PetscObject) _dgxzP, "_dgxzP");
  VecSet(_dgxzP,0.0);

  VecDuplicate(_uP,&_gTxyP); VecSet(_gTxyP,0.0);
  VecDuplicate(_uP,&_gTxzP); VecSet(_gTxzP,0.0);

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

// set off-fault material properties
PetscErrorCode PowerLaw::setMaterialParameters()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::setMaterialParameters";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif


  // set each field using it's vals and depths std::vectors
  if (_Nz == 1) {
    VecSet(_A,_AVals[0]);
    VecSet(_B,_BVals[0]);
    VecSet(_n,_nVals[0]);
  }
  else {
    if (_viscDistribution.compare("mms")==0) {
      //~ mapToVec(_A,MMS_A,_Nz,_dy,_dz);
      //~ mapToVec(_B,MMS_B,_Nz,_dy,_dz);
      //~ mapToVec(_n,MMS_n,_Nz,_dy,_dz);

      if (_Nz == 1) { mapToVec(_A,MMS_A1D,*_y); }
      else { mapToVec(_A,MMS_A,*_y,*_z); }
      if (_Nz == 1) { mapToVec(_B,MMS_B1D,*_y); }
      else { mapToVec(_B,MMS_B,*_y,*_z); }
      if (_Nz == 1) { mapToVec(_n,MMS_n1D,*_y); }
      else { mapToVec(_n,MMS_n,*_y,*_z); }
    }
    else if (_viscDistribution.compare("loadFromFile")==0) { loadEffViscFromFiles(); }
    else {
      ierr = setVecFromVectors(_A,_AVals,_ADepths);CHKERRQ(ierr);
      ierr = setVecFromVectors(_B,_BVals,_BDepths);CHKERRQ(ierr);
      ierr = setVecFromVectors(_n,_nVals,_nDepths);CHKERRQ(ierr);
    }
  }
  _he.getTemp(_T);
  //~ guessSteadyStateEffVisc();


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
return ierr;
}


//parse input file and load values into data members
PetscErrorCode PowerLaw::loadEffViscFromFiles()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::loadEffViscFromFiles()";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  PetscViewer inv; // in viewer

  // load effective viscosity
  string vecSourceFile = _inputDir + "EffVisc";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_effVisc,inv);CHKERRQ(ierr);

  // load A
  vecSourceFile = _inputDir + "A";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_A,inv);CHKERRQ(ierr);

  // load B
  vecSourceFile = _inputDir + "B";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_B,inv);CHKERRQ(ierr);

  // load B
  vecSourceFile = _inputDir + "n";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_n,inv);CHKERRQ(ierr);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

//parse input file and load values into data members
PetscErrorCode PowerLaw::loadFieldsFromFiles()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::loadFieldsFromFiles()";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  PetscViewer inv; // in viewer


  /*  // load T (initial condition)
  ierr = VecCreate(PETSC_COMM_WORLD,&_T);CHKERRQ(ierr);
  ierr = VecSetSizes(_T,PETSC_DECIDE,_Ny*_Nz);CHKERRQ(ierr);
  ierr = VecSetFromOptions(_T);
  PetscObjectSetName((PetscObject) _T, "_T");
  ierr = loadVecFromInputFile(_T,_inputDir,_TFile);CHKERRQ(ierr);
  */


  // load bcL
  string vecSourceFile = _inputDir + "bcL";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_bcLP,inv);CHKERRQ(ierr);

  //~ // load bcR
  vecSourceFile = _inputDir + "bcR";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_bcRPShift,inv);CHKERRQ(ierr);
  VecSet(_bcRP,0.0);

  // load gxy
  vecSourceFile = _inputDir + "Gxy";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_gxyP,inv);CHKERRQ(ierr);

  // load gxz
  vecSourceFile = _inputDir + "Gxz";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_gxzP,inv);CHKERRQ(ierr);


   // load sxy
  vecSourceFile = _inputDir + "Sxy";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_sxyP,inv);CHKERRQ(ierr);

  // load sxz
  vecSourceFile = _inputDir + "Sxz";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_sxzP,inv);CHKERRQ(ierr);


  // load effective viscosity
  vecSourceFile = _inputDir + "EffVisc";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_effVisc,inv);CHKERRQ(ierr);

  // load temperature
  vecSourceFile = _inputDir + "T";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_T,inv);CHKERRQ(ierr);

  // load power law parameters
  vecSourceFile = _inputDir + "A";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_A,inv);CHKERRQ(ierr);

  vecSourceFile = _inputDir + "B";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_B,inv);CHKERRQ(ierr);

  vecSourceFile = _inputDir + "n";
  ierr = PetscViewerCreate(PETSC_COMM_WORLD,&inv);CHKERRQ(ierr);
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,vecSourceFile.c_str(),FILE_MODE_READ,&inv);CHKERRQ(ierr);
  ierr = PetscViewerSetFormat(inv,PETSC_VIEWER_BINARY_MATLAB);CHKERRQ(ierr);
  ierr = VecLoad(_n,inv);CHKERRQ(ierr);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// try to speed up spin up by starting closer to steady state
PetscErrorCode PowerLaw::setSSInitialConds(Domain& D)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::setSSInitialConds";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  delete _sbpP;

  // set up SBP operators
  //~ string bcT,string bcR,string bcB, string bcL
  std::string bcTType = "Neumann";
  std::string bcBType = "Neumann";
  std::string bcRType = "Dirichlet";
  std::string bcLType = "Neumann";

  if (_sbpType.compare("mc")==0) {
    _sbpP = new SbpOps_c(D,D._muVecP,bcTType,bcRType,bcBType,bcLType,"yz");
  }
  else if (_sbpType.compare("mfc")==0) {
    _sbpP = new SbpOps_fc(D,D._muVecP,bcTType,bcRType,bcBType,bcLType,"yz"); // to spin up viscoelastic
  }
  else if (_sbpType.compare("mfc_coordTrans")==0) {
    _sbpP = new SbpOps_fc_coordTrans(D,D._muVecP,bcTType,bcRType,bcBType,bcLType,"yz");
  }
  else {
    PetscPrintf(PETSC_COMM_WORLD,"ERROR: SBP type type not understood\n");
    assert(0); // automatically fail
  }
  KSPDestroy(&_kspP);
  KSPCreate(PETSC_COMM_WORLD,&_kspP);
  setupKSP(_sbpP,_kspP,_pcP);

  // set up boundary conditions
  VecSet(_bcRP,0.0);
  PetscInt    Istart,Iend;
  PetscScalar v = 0;
  Vec faultVisc; VecDuplicate(_bcLP,&faultVisc);
  VecGetOwnershipRange(_effVisc,&Istart,&Iend);
  for (PetscInt Ii=Istart;Ii<Iend;Ii++) {
    if (Ii < _Nz) {
      VecGetValues(_effVisc,1,&Ii,&v);
      VecSetValue(faultVisc,Ii,v,INSERT_VALUES);
    }
  }
  VecAssemblyBegin(faultVisc); VecAssemblyEnd(faultVisc);

  VecGetOwnershipRange(_bcLP,&Istart,&Iend);
  for (PetscInt Ii=Istart;Ii<Iend;Ii++) {
    PetscScalar tauRS = _fault.getTauSS(Ii); // rate-and-state strength

    // viscous strength
    VecGetValues(faultVisc,1,&Ii,&v);
    PetscScalar tauVisc = v*_vL/2.0/10.0; // 10 = seismogenic depth

    PetscScalar tau = min(tauRS,tauVisc);
    //~ PetscScalar tau = tauRS;
    //~ PetscScalar tau = tauVisc;
    VecSetValue(_bcLP,Ii,tau,INSERT_VALUES);
  }
  VecAssemblyBegin(_bcLP); VecAssemblyEnd(_bcLP);

  _sbpP->setRhs(_rhsP,_bcLP,_bcRP,_bcTP,_bcBP);
  ierr = KSPSolve(_kspP,_rhsP,_uP);CHKERRQ(ierr);
  KSPDestroy(&_kspP);
  VecDestroy(&faultVisc);
  delete _sbpP;
  _sbpP = NULL;

  // extract boundary condition information from u
  PetscScalar minVal = 0;
  VecMin(_uP,NULL,&minVal);
  ierr = VecGetOwnershipRange(_uP,&Istart,&Iend);CHKERRQ(ierr);
  for (PetscInt Ii=Istart;Ii<Iend;Ii++) {
    // put left boundary info into fault slip vector
    if ( Ii < _Nz ) {
      ierr = VecGetValues(_uP,1,&Ii,&v);CHKERRQ(ierr);
      v += 2.*(abs(minVal) + 1.0);
      //~ ierr = VecSetValues(_bcLP,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
      ierr = VecSetValues(_fault._slip,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
    }

    // put right boundary data into bcR
    if ( Ii > (_Ny*_Nz - _Nz - 1) ) {
      PetscInt zI =  Ii - (_Ny*_Nz - _Nz);
      ierr = VecGetValues(_uP,1,&Ii,&v);CHKERRQ(ierr);
      v += abs(minVal) + 1.0;
      ierr = VecSetValues(_bcRPShift,1,&zI,&v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(_bcRPShift);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_fault._slip);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcRPShift);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_fault._slip);CHKERRQ(ierr);
  VecCopy(_bcRPShift,_bcRP);

  //~ VecCopy(_fault._slip,*(_var.begin()+2));
  VecCopy(_fault._slip,_varEx["slip"]);
  if (_bcLTauQS==0) {
    VecCopy(_fault._slip,_bcLP);
    VecScale(_bcLP,0.5);
  }

  // reset all BCs
  //~ VecSet(_bcRPShift,0.0);
  //~ VecSet(_bcRPShift,13.0);
  //~ VecSet(_bcRP,_vL*_initTime/2.0);
  //~ VecSet(_bcLP,0.0);
  //~ VecSet(_fault._slip,0.0);
  //~ VecCopy(_fault._slip,*(_var.begin()+2));
  //~ VecSet(_uP,0.0);


  return ierr;
  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
}


// inititialize effective viscosity
PetscErrorCode PowerLaw::guessSteadyStateEffVisc()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::guessSteadyStateEffVisc";
    PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
  #endif

  PetscScalar strainRate = 1e-12,s=0.; // guess
  PetscScalar *A,*B,*n,*T,*effVisc;
  PetscInt Ii,Istart,Iend;
  VecGetOwnershipRange(_effVisc,&Istart,&Iend);
  VecGetArray(_A,&A);
  VecGetArray(_B,&B);
  VecGetArray(_n,&n);
  VecGetArray(_T,&T);
  VecGetArray(_effVisc,&effVisc);
  PetscInt Jj = 0;
  for (Ii=Istart;Ii<Iend;Ii++) {
    s = pow(strainRate/(A[Jj]*exp(-B[Jj]/T[Jj])),1.0/n[Jj]);
    effVisc[Jj] =  s/strainRate* 1e-3; // (GPa s)  in terms of strain rate
    Jj++;
  }
  VecRestoreArray(_A,&A);
  VecRestoreArray(_B,&B);
  VecRestoreArray(_n,&n);
  VecRestoreArray(_T,&T);
  VecRestoreArray(_effVisc,&effVisc);

  return ierr;
  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
  #endif
}


PetscErrorCode PowerLaw::setMMSInitialConditions()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::setMMSInitialConditions()";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);CHKERRQ(ierr);
  #endif

  PetscScalar time = _initTime;
  if (_Nz == 1) { mapToVec(_gxyP,MMS_gxy1D,*_y,time); }
  else { mapToVec(_gxyP,MMS_gxy,*_y,*_z,time); }
  if (_Nz == 1) { VecSet(_gxzP,0.0); }
  else { mapToVec(_gxzP,MMS_gxz,*_y,*_z,time); }

  //~ ierr = VecCopy(_gxyP,*(_var.begin()+3)); CHKERRQ(ierr);
  //~ ierr = VecCopy(_gxzP,*(_var.begin()+4)); CHKERRQ(ierr);
  ierr = VecCopy(_gxyP,_varEx["gVxy"]); CHKERRQ(ierr);
  ierr = VecCopy(_gxzP,_varEx["gVxz"]); CHKERRQ(ierr);

  // set material properties
  if (_Nz == 1) { mapToVec(_muVecP,MMS_mu1D,*_y); }
  else { mapToVec(_muVecP,MMS_mu,*_y,*_z); }
  if (_Nz == 1) { mapToVec(_A,MMS_A1D,*_y); }
  else { mapToVec(_A,MMS_A,*_y,*_z); }
  if (_Nz == 1) { mapToVec(_B,MMS_B1D,*_y); }
  else { mapToVec(_B,MMS_B,*_y,*_z); }
  if (_Nz == 1) { mapToVec(_n,MMS_n1D,*_y); }
  else { mapToVec(_n,MMS_n,*_y,*_z); }
  if (_Nz == 1) { mapToVec(_T,MMS_T1D,*_y); }
  else { mapToVec(_T,MMS_T,*_y,*_z); }

  // create rhs: set boundary conditions, set rhs, add source terms
  ierr = setMMSBoundaryConditions(time);CHKERRQ(ierr); // modifies _bcLP,_bcRP,_bcTP, and _bcBP
  ierr = _sbpP->setRhs(_rhsP,_bcLP,_bcRP,_bcTP,_bcBP);CHKERRQ(ierr);

  Vec viscSourceMMS,HxviscSourceMMS,viscSource,uSource,HxuSource;
  ierr = VecDuplicate(_uP,&viscSource); CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&viscSourceMMS); CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&HxviscSourceMMS); CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&uSource); CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&HxuSource); CHKERRQ(ierr);

  //~ ierr = setViscStrainSourceTerms(viscSource,*(_var.begin()+3),*(_var.begin()+4));CHKERRQ(ierr);
  ierr = setViscStrainSourceTerms(viscSource,_varEx["gVxy"],_varEx["gVxy"]);CHKERRQ(ierr);
  if (_Nz == 1) { mapToVec(viscSourceMMS,MMS_gSource1D,*_y,_currTime); }
  else { mapToVec(viscSourceMMS,MMS_gSource,*_y,*_z,_currTime); }
  ierr = _sbpP->H(viscSourceMMS,HxviscSourceMMS); CHKERRQ(ierr);
  VecDestroy(&viscSourceMMS);
  if (_Nz == 1) { mapToVec(uSource,MMS_uSource1D,*_y,_currTime); }
  else { mapToVec(uSource,MMS_uSource,*_y,*_z,_currTime); }
  ierr = _sbpP->H(uSource,HxuSource); CHKERRQ(ierr);
  VecDestroy(&uSource);
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Mat qy,rz,yq,zr;
    ierr = _sbpP->getCoordTrans(qy,rz,yq,zr); CHKERRQ(ierr);
    ierr = multMatsVec(yq,zr,viscSource); CHKERRQ(ierr);
    ierr = multMatsVec(yq,zr,HxviscSourceMMS); CHKERRQ(ierr);
    ierr = multMatsVec(yq,zr,HxuSource); CHKERRQ(ierr);
  }

  ierr = VecAXPY(_rhsP,1.0,viscSource); CHKERRQ(ierr); // add d/dy mu*epsVxy + d/dz mu*epsVxz
  ierr = VecAXPY(_rhsP,1.0,HxviscSourceMMS); CHKERRQ(ierr); // add MMS source for viscous strains
  ierr = VecAXPY(_rhsP,1.0,HxuSource); CHKERRQ(ierr); // add MMS source for u
  VecDestroy(&viscSource);
  VecDestroy(&HxviscSourceMMS);
  VecDestroy(&HxuSource);


  // solve for displacement
  double startTime = MPI_Wtime();
  ierr = KSPSolve(_kspP,_rhsP,_uP); CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;
  ierr = setSurfDisp(); CHKERRQ(ierr);

  // set stresses
  ierr = setStresses(time); CHKERRQ(ierr);


  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s.\n",funcName.c_str(),FILENAME);
  #endif
  return ierr;
}

PetscErrorCode PowerLaw::integrate()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  double startTime = MPI_Wtime();

  // ensure max time step is limited by Maxwell time
  PetscScalar maxTimeStep_tot, maxDeltaT_Tmax = 0.0;
  computeMaxTimeStep(maxDeltaT_Tmax);
  maxTimeStep_tot = min(_maxDeltaT,maxDeltaT_Tmax);

  _stepCount++;
  if (_timeIntegrator.compare("IMEX")==0) {
    _quadImex->setTolerance(_atol);CHKERRQ(ierr);
    _quadImex->setTimeStepBounds(_minDeltaT,maxTimeStep_tot);CHKERRQ(ierr);
    ierr = _quadImex->setTimeRange(_initTime,_maxTime);
    ierr = _quadImex->setInitialConds(_varEx,_varIm);CHKERRQ(ierr);

    // control which fields are used to select step size
    ierr = _quadImex->setErrInds(_timeIntInds);
    if (_bcLTauQS==1) {
      //~ int arrInds[] = {3,4}; // state: 0, slip: 1
      const char* tempList[] = {"gVxy","gVxz"};
      std::vector<std::string> errInds(tempList,tempList+2);
      ierr = _quadImex->setErrInds(errInds);
    }

    ierr = _quadImex->integrate(this);CHKERRQ(ierr);
  }
  else { // fully explicit time integration
    // call odeSolver routine integrate here
    _quadEx->setTolerance(_atol);CHKERRQ(ierr);
    _quadEx->setTimeStepBounds(_minDeltaT,maxTimeStep_tot);CHKERRQ(ierr);
    ierr = _quadEx->setTimeRange(_initTime,_maxTime);
    ierr = _quadEx->setInitialConds(_varEx);CHKERRQ(ierr);

    // control which fields are used to select step size
    if (_isMMS) {
      //~ int arrInds[] = {3,4}; // state: 0, slip: 1
      //~ std::vector<int> errInds(arrInds,arrInds+1); // !! UPDATE THIS LINE TOO
      const char* tempList[] = {"gVxy","gVxz"};
      std::vector<string> errInds(tempList,tempList+2);
      ierr = _quadEx->setErrInds(errInds);
    }
    else if (_bcLTauQS==1) {
      //~ int arrInds[] = {3,4}; // state: 0, slip: 1
      //~ std::vector<int> errInds(arrInds,arrInds+2); // !! UPDATE THIS LINE TOO
      const char* tempList[] = {"gVxy","gVxz"};
      std::vector<string> errInds(tempList,tempList+2);
      ierr = _quadEx->setErrInds(errInds);
    }
    else  {
      ierr = _quadEx->setErrInds(_timeIntInds);
    }
    ierr = _quadEx->integrate(this);CHKERRQ(ierr);
  }

  _integrateTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// limited by Maxwell time
PetscErrorCode PowerLaw::computeMaxTimeStep(PetscScalar& maxTimeStep)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::computeMaxTimeStep";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  Vec Tmax;
  VecDuplicate(_uP,&Tmax);
  VecSet(Tmax,0.0);
  VecPointwiseDivide(Tmax,_effVisc,_muVecP);
  PetscScalar min_Tmax;
  VecMin(Tmax,NULL,&min_Tmax);

  maxTimeStep = 0.3 * min_Tmax;

  VecDestroy(&Tmax);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


PetscErrorCode PowerLaw::d_dt(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;
  if (_isMMS) {
    ierr = d_dt_mms(time,varEx,dvarEx);CHKERRQ(ierr);
  }
  else {
    ierr = d_dt_eqCycle(time,varEx,dvarEx);CHKERRQ(ierr);
  }

  if (_heatEquationType.compare("steadyState")==0 ) {
    //~ ierr = _he.computeSteadyStateTemp(time,*(dvarBegin+2),_fault._tauQSP,_sigmadev,*(dvarBegin+3),
    //~ *(dvarBegin+4),_T);
    ierr = _he.computeSteadyStateTemp(time,dvarEx["slip"],_fault._tauQSP,_sigmadev,dvarEx["gVxy"],
    dvarEx["gVxz"],_T);
  }

  return ierr;
}

// implicit/explicit time stepping
PetscErrorCode PowerLaw::d_dt(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx,
      map<string,Vec>& varIm,const map<string,Vec>& varImo,const PetscScalar dt)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::d_dt IMEX";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  if (_thermalCoupling.compare("coupled")==0 ) {
    //~ VecCopy(*varBeginImo,_T);
    VecCopy(varImo.find("deltaT")->second,_T);
    _he.setTemp(_T);
    _he.getTemp(_T);
  }

  ierr = d_dt_eqCycle(time,varEx,dvarEx);CHKERRQ(ierr);

  //~ if (_heatEquationType.compare("transient")==0 ) {
  //~ ierr = _he.be(time,*(dvarBegin+2),_fault._tauQSP,_sigmadev,*(dvarBegin+3),
    //~ *(dvarBegin+4),*varBeginIm,*varBeginImo,dt);CHKERRQ(ierr);
  // arguments:
  // time, slipVel, sigmadev, dgxy, dgxz, T, dTdt
  ierr = _he.be(time,dvarEx.find("slip")->second,_fault._tauQSP,_sigmadev,dvarEx.find("gVxy")->second,
    dvarEx.find("gVxz")->second,varIm.find("deltaT")->second,varImo.find("deltaT")->second,dt);CHKERRQ(ierr);



#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode PowerLaw::d_dt_eqCycle(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
//~ double startMiscTime = MPI_Wtime();
//~ _miscTime += MPI_Wtime() - startMiscTime;

  //~ VecCopy(*(varBegin+3),_gxyP);
  //~ VecCopy(*(varBegin+4),_gxzP);
  VecCopy(varEx.find("gVxy")->second,_gxyP);
  VecCopy(varEx.find("gVxz")->second,_gxzP);

  // update boundaries
  if (_bcLTauQS==0) {
    //~ ierr = VecCopy(*(varBegin+2),_bcLP);CHKERRQ(ierr);
    ierr = VecCopy(varEx.find("slip")->second,_bcLP);CHKERRQ(ierr);
    ierr = VecScale(_bcLP,0.5);CHKERRQ(ierr);
  } // else do nothing
  ierr = VecSet(_bcRP,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_bcRP,1.0,_bcRPShift);CHKERRQ(ierr);


  // add source terms to rhs: d/dy( 2*mu*strainV_xy) + d/dz( 2*mu*strainV_xz)
  Vec viscSource;
  ierr = VecDuplicate(_gxyP,&viscSource);CHKERRQ(ierr);
  ierr = VecSet(viscSource,0.0);CHKERRQ(ierr);
  ierr = setViscStrainSourceTerms(viscSource,_gxyP,_gxzP);CHKERRQ(ierr);

  // set up rhs vector
  ierr = _sbpP->setRhs(_rhsP,_bcLP,_bcRP,_bcTP,_bcBP);CHKERRQ(ierr); // update rhs from BCs
  ierr = VecAXPY(_rhsP,1.0,viscSource);CHKERRQ(ierr);
  VecDestroy(&viscSource);

  // solve for displacement
  double startTime = MPI_Wtime();
  ierr = KSPSolve(_kspP,_rhsP,_uP);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;


  // update stresses, viscosity, and set shear traction on fault
  ierr = setStresses(time);CHKERRQ(ierr);
  ierr = _fault.setTauQS(_sxyP,NULL);CHKERRQ(ierr);
  computeViscosity();



  // set rates
  if (_bcLTauQS==0) {
    ierr = _fault.d_dt(varEx,dvarEx); // sets rates for slip and state
  }
  else {
    //~ VecSet(*dvarBegin,0.0); // dstate psi
    //~ VecSet(*(dvarBegin+1),0.0); // // dstate theta
    //~ VecSet(*(dvarBegin+2),0.0); // slip vel
    VecSet(dvarEx["psi"],0.0); // dstate psi
    VecSet(dvarEx["slip"],0.0); // slip vel
  }
  //~ VecSet(*dvarBegin,0.0); // dstate psi
  //~ VecSet(*(dvarBegin+1),0.0);

  ierr = setViscStrainRates(time,_gxyP,_gxzP,dvarEx["gVxy"],dvarEx["gVxz"]); CHKERRQ(ierr);


  //~VecSet(*dvarBegin,0.0);
  //~VecSet(*(dvarBegin+1),0.0);
  //~VecSet(*(dvarBegin+2),0.0);
  //~ VecSet(*(dvarBegin+3),0.0);
  //~ VecSet(*(dvarBegin+4),0.0);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
      CHKERRQ(ierr);
  #endif
  return ierr;
}

PetscErrorCode PowerLaw::d_dt_mms(const PetscScalar time,const map<string,Vec>& varEx,map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::d_dt_mms";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  _currTime = time;

  //~ ierr = VecCopy(*(varBegin+3),_gxyP); CHKERRQ(ierr);
  //~ ierr = VecCopy(*(varBegin+4),_gxzP); CHKERRQ(ierr);
  VecCopy(varEx.find("gVxy")->second,_gxyP);
  VecCopy(varEx.find("gVxz")->second,_gxzP);

  // force viscous strains to be correct
  //~ if (_Nz == 1) { mapToVec(_gxyP,MMS_gxy1D,*_y,time); }
  //~ else { mapToVec(_gxyP,MMS_gxy,*_y,*_z,time); }
  //~ if (_Nz == 1) { mapToVec(_gxzP,MMS_gxy1D,*_y,time); }
  //~ else { mapToVec(_gxzP,MMS_gxz,*_y,*_z,time); }

  // create rhs: set boundary conditions, set rhs, add source terms
  ierr = setMMSBoundaryConditions(time); CHKERRQ(ierr); // modifies _bcLP,_bcRP,_bcTP, and _bcBP
  ierr = _sbpP->setRhs(_rhsP,_bcLP,_bcRP,_bcTP,_bcBP); CHKERRQ(ierr);

  Vec viscSourceMMS,HxviscSourceMMS,viscSource,uSource,HxuSource;
  ierr = VecDuplicate(_uP,&viscSource); CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&viscSourceMMS); CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&HxviscSourceMMS); CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&uSource); CHKERRQ(ierr);
  ierr = VecDuplicate(_uP,&HxuSource); CHKERRQ(ierr);

  //~ ierr = setViscStrainSourceTerms(viscSource,_var.begin());CHKERRQ(ierr);
  ierr = setViscStrainSourceTerms(viscSource,_gxyP,_gxzP); CHKERRQ(ierr);
  if (_Nz == 1) { mapToVec(viscSourceMMS,MMS_gSource1D,*_y,time); }
  else { mapToVec(viscSourceMMS,MMS_gSource,*_y,*_z,time); }
  ierr = _sbpP->H(viscSourceMMS,HxviscSourceMMS);
  VecDestroy(&viscSourceMMS);
  if (_Nz == 1) { mapToVec(uSource,MMS_uSource1D,*_y,time); }
  else { mapToVec(uSource,MMS_uSource,*_y,*_z,time); }
  ierr = _sbpP->H(uSource,HxuSource);
  VecDestroy(&uSource);
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Mat qy,rz,yq,zr;
    ierr = _sbpP->getCoordTrans(qy,rz,yq,zr); CHKERRQ(ierr);
    ierr = multMatsVec(yq,zr,HxviscSourceMMS); CHKERRQ(ierr);
    ierr = multMatsVec(yq,zr,HxuSource); CHKERRQ(ierr);
  }

  ierr = VecAXPY(_rhsP,1.0,viscSource); CHKERRQ(ierr); // add d/dy mu*epsVxy + d/dz mu*epsVxz
  ierr = VecAXPY(_rhsP,1.0,HxviscSourceMMS); CHKERRQ(ierr); // add MMS source for viscous strains
  ierr = VecAXPY(_rhsP,1.0,HxuSource); CHKERRQ(ierr); // add MMS source for u
  VecDestroy(&HxviscSourceMMS);
  VecDestroy(&HxuSource);


  double startTime = MPI_Wtime();
  ierr = KSPSolve(_kspP,_rhsP,_uP); CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;
  ierr = setSurfDisp();

  //~ mapToVec(_uP,MMS_uA,*_y,*_z,time);

  // update stresses
  ierr = setStresses(time); CHKERRQ(ierr);
  //~ mapToVec(_sxyP,MMS_pl_sigmaxy,*_y,*_z,_currTime);
  //~ mapToVec(_sxzP,MMS_pl_sigmaxz,*_y,*_z,_currTime);
  //~ mapToVec(_sigmadev,MMS_sigmadev,*_y,*_z,_currTime);
  computeViscosity();

  // update rates
  //~ ierr = setViscStrainRates(time,_gxyP,_gxzP,*(dvarBegin+3),*(dvarBegin+4)); CHKERRQ(ierr);
  ierr = setViscStrainRates(time,_gxyP,_gxzP,dvarEx["gVxy"],dvarEx["gVxz"]); CHKERRQ(ierr);
  Vec source;
  VecDuplicate(_uP,&source);
  if (_Nz == 1) { mapToVec(source,MMS_pl_gxy_t_source1D,*_y,_currTime); }
  else { mapToVec(source,MMS_pl_gxy_t_source,*_y,*_z,_currTime); }
  //~ VecAXPY(*(dvarBegin+3),1.0,source);
  VecAXPY(dvarEx["gVxy"],1.0,source);
  if (_Nz == 1) { mapToVec(source,MMS_pl_gxz_t_source1D,*_y,_currTime); }
  else { mapToVec(source,MMS_pl_gxz_t_source,*_y,*_z,_currTime); }
  //~ VecAXPY(*(dvarBegin+4),1.0,source);
  VecAXPY(dvarEx["gVxz"],1.0,source);
  VecDestroy(&source);


  // update rates
  //~ VecSet(*dvarBegin,0.0); // d/dt psi
  //~ VecSet(*(dvarBegin+1),0.0); // d/dt theta
  //~ VecSet(*(dvarBegin+2),0.0); // slip vel
  VecSet(dvarEx["psi"],0.0); // dstate psi
  VecSet(dvarEx["slip"],0.0); // slip vel

  //~ mapToVec(*(dvarBegin+3),MMS_gxy_t,*_y,*_z,time);
  //~ mapToVec(*(dvarBegin+4),MMS_gxz_t,*_y,*_z,time);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


//~ PetscErrorCode PowerLaw::setViscStrainSourceTerms(Vec& out,const_it_vec varBegin)
PetscErrorCode PowerLaw::setViscStrainSourceTerms(Vec& out,Vec& gxy, Vec& gxz)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::setViscStrainSourceTerms";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  Vec source;
  VecDuplicate(gxy,&source);
  VecSet(source,0.0);

  // add source terms to rhs: d/dy( mu*gxy) + d/dz( mu*gxz)
  // + Hz^-1 E0z mu gxz - Hz^-1 ENz mu gxz
  Vec sourcexy_y;
  VecDuplicate(_uP,&sourcexy_y);
  VecSet(sourcexy_y,0.0);
  ierr = _sbpP->Dyxmu(gxy,sourcexy_y);CHKERRQ(ierr);

  // if bcL is shear stress, then also add Hy^-1 E0y mu gxy
  if (_bcLTauQS==1) {
    Vec temp1,bcL;
    VecDuplicate(gxy,&temp1); VecSet(temp1,0.0);
    VecDuplicate(gxy,&bcL); VecSet(bcL,0.0);
    _sbpP->HyinvxE0y(gxy,temp1);
    ierr = VecPointwiseMult(bcL,_muVecP,temp1); CHKERRQ(ierr);
    VecDestroy(&temp1);
    ierr = VecAXPY(sourcexy_y,1.0,bcL);CHKERRQ(ierr);
    VecDestroy(&bcL);
  }

  ierr = VecCopy(sourcexy_y,source);CHKERRQ(ierr); // sourcexy_y -> source
  VecDestroy(&sourcexy_y);

  if (_Nz > 1)
  {
    Vec sourcexz_z;
    VecDuplicate(gxz,&sourcexz_z);
    ierr = _sbpP->Dzxmu(gxz,sourcexz_z);CHKERRQ(ierr);
    ierr = VecAXPY(source,1.0,sourcexz_z);CHKERRQ(ierr); // source += Hxsourcexz_z
    VecDestroy(&sourcexz_z);

    // enforce traction boundary condition
    Vec temp1,bcT,bcB;
    VecDuplicate(gxz,&temp1); VecSet(temp1,0.0);
    VecDuplicate(gxz,&bcT);
    VecDuplicate(gxz,&bcB);

    _sbpP->HzinvxE0z(gxz,temp1);
    ierr = VecPointwiseMult(bcT,_muVecP,temp1); CHKERRQ(ierr);

    _sbpP->HzinvxENz(gxz,temp1);
    ierr = VecPointwiseMult(bcB,_muVecP,temp1); CHKERRQ(ierr);

    ierr = VecAXPY(source,1.0,bcT);CHKERRQ(ierr);
    ierr = VecAXPY(source,-1.0,bcB);CHKERRQ(ierr);

    VecDestroy(&temp1);
    VecDestroy(&bcT);
    VecDestroy(&bcB);
  }

  // apply effects of coordinate transform
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Mat qy,rz,yq,zr;
    ierr = _sbpP->getCoordTrans(qy,rz,yq,zr); CHKERRQ(ierr);
    ierr = multMatsVec(yq,zr,source); CHKERRQ(ierr);
  }
  ierr = _sbpP->H(source,out); CHKERRQ(ierr);
  VecDestroy(&source);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
      CHKERRQ(ierr);
  #endif
  return ierr = 0;
}


PetscErrorCode PowerLaw::computeViscosity()
{
    PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::computeViscosity";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  //~ // compute effective viscosity
  //~ PetscScalar sigmadev,A,B,n,T,effVisc=0;
  //~ PetscInt Ii,Istart,Iend;
  //~ VecGetOwnershipRange(_effVisc,&Istart,&Iend);
  //~ for (Ii=Istart;Ii<Iend;Ii++) {
    //~ VecGetValues(_sigmadev,1,&Ii,&sigmadev);
    //~ VecGetValues(_A,1,&Ii,&A);
    //~ VecGetValues(_B,1,&Ii,&B);
    //~ VecGetValues(_n,1,&Ii,&n);
    //~ VecGetValues(_T,1,&Ii,&T);
    //~ effVisc = 1e-3 / ( A*pow(sigmadev,n-1.0)*exp(-B/T) ) ;
    //~ VecSetValues(_effVisc,1,&Ii,&effVisc,INSERT_VALUES);

    //~ assert(~isnan(effVisc));
    //~ assert(~isinf(effVisc));
  //~ }
  //~ VecAssemblyBegin(_effVisc);
  //~ VecAssemblyEnd(_effVisc);


  // compute effective viscosity
  PetscScalar *sigmadev,*A,*B,*n,*T,*effVisc=0;
  PetscInt Ii,Istart,Iend;
  VecGetOwnershipRange(_effVisc,&Istart,&Iend);
  VecGetArray(_sigmadev,&sigmadev);
  VecGetArray(_A,&A);
  VecGetArray(_B,&B);
  VecGetArray(_n,&n);
  VecGetArray(_T,&T);
  VecGetArray(_effVisc,&effVisc);
  PetscInt Jj = 0;
  for (Ii=Istart;Ii<Iend;Ii++) {
    effVisc[Jj] = 1e-3 / ( A[Jj]*pow(sigmadev[Jj],n[Jj]-1.0)*exp(-B[Jj]/T[Jj]) ) ;

    assert(~isnan(effVisc[Jj]));
    assert(~isinf(effVisc[Jj]));
    Jj++;
  }
  VecRestoreArray(_sigmadev,&sigmadev);
  VecRestoreArray(_A,&A);
  VecRestoreArray(_B,&B);
  VecRestoreArray(_n,&n);
  VecRestoreArray(_T,&T);
  VecRestoreArray(_effVisc,&effVisc);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr = 0;
}


//~ PetscErrorCode PowerLaw::setViscStrainRates(const PetscScalar time,const_it_vec varBegin,it_vec dvarBegin)
PetscErrorCode PowerLaw::setViscStrainRates(const PetscScalar time,const Vec& gVxy, const Vec& gVxz,
  Vec& gVxy_t, Vec& gVxz_t)
{
    PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::setViscStrainRates";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

// add SAT terms to strain rate for epsxy
  Vec SAT;
  VecDuplicate(_gTxyP,&SAT);
  ierr = setViscousStrainRateSAT(_uP,_bcLP,_bcRP,SAT);CHKERRQ(ierr);
  VecSet(SAT,0.0);

  // d/dt gxy = sxy/visc + qy*mu/visc*SAT
  VecPointwiseMult(gVxy_t,_muVecP,SAT);
  if (_sbpType.compare("mfc_coordTrans")==0) {
    Mat qy,rz,yq,zr;
    Vec temp1;
    VecDuplicate(_gxyP,&temp1);
    ierr = _sbpP->getCoordTrans(qy,rz,yq,zr); CHKERRQ(ierr);
    MatMult(qy,gVxy_t,temp1); // correct
    //~ MatMult(yq,gVxy_t,temp1); // incorrect
    VecCopy(temp1,gVxy_t);
    VecDestroy(&temp1);
  }
  VecAXPY(gVxy_t,1.0,_sxyP);
  VecPointwiseDivide(gVxy_t,gVxy_t,_effVisc);


  if (_Nz > 1) {
    VecCopy(_sxzP,gVxz_t);
    VecPointwiseDivide(gVxz_t,gVxz_t,_effVisc);
  }

  VecDestroy(&SAT);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr = 0;
}


PetscErrorCode PowerLaw::setViscousStrainRateSAT(Vec &u, Vec &gL, Vec &gR, Vec &out)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::viscousStrainRateSAT";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  VecSet(out,0.0);

  Vec GL, GR,temp1;
  VecDuplicate(u,&GL); VecSet(GL,0.0);
  VecDuplicate(u,&GR); VecSet(GR,0.0);
  VecDuplicate(u,&temp1); VecSet(temp1,0.0);

  // left displacement boundary
  if (_bcLTauQS==0) {
    ierr = _sbpP->HyinvxE0y(u,temp1);CHKERRQ(ierr);
    ierr = _sbpP->Hyinvxe0y(gL,GL);CHKERRQ(ierr);
    VecAXPY(out,1.0,temp1);
    VecAXPY(out,-1.0,GL);
  }

  // right displacement boundary
  VecSet(temp1,0.0);
  ierr = _sbpP->HyinvxENy(u,temp1);CHKERRQ(ierr);
  ierr = _sbpP->HyinvxeNy(gR,GR);CHKERRQ(ierr);
  VecAXPY(out,-1.0,temp1);
  VecAXPY(out,1.0,GR);

  VecDestroy(&GL);
  VecDestroy(&GR);
  VecDestroy(&temp1);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
      CHKERRQ(ierr);
  #endif
  return ierr = 0;
}

// computes sigmaxy, sigmaxz, and sigmadev = sqrt(sigmaxy^2 + sigmaxz^2)
PetscErrorCode PowerLaw::setStresses(const PetscScalar time)
{
    PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::setStresses";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif

  _sbpP->Dy(_uP,_gTxyP);
  VecCopy(_gTxyP,_sxyP);
  VecAXPY(_sxyP,-1.0,_gxyP);
  VecPointwiseMult(_sxyP,_sxyP,_muVecP);

  // deviatoric stress: part 1/3
  VecPointwiseMult(_sigmadev,_sxyP,_sxyP);

  if (_Nz > 1) {
    _sbpP->Dz(_uP,_gTxzP);
    VecCopy(_gTxzP,_sxzP);
    VecAXPY(_sxzP,-1.0,_gxzP);
    VecPointwiseMult(_sxzP,_sxzP,_muVecP);

  // deviatoric stress: part 2/3
  Vec temp;
  VecDuplicate(_sxzP,&temp);
  VecPointwiseMult(temp,_sxzP,_sxzP);
  VecAXPY(_sigmadev,1.0,temp);
  VecDestroy(&temp);
  }

  // deviatoric stress: part 3/3
  VecSqrtAbs(_sigmadev);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s: time=%.15e\n",funcName.c_str(),FILENAME,time);
    CHKERRQ(ierr);
  #endif
  return ierr = 0;
}



PetscErrorCode PowerLaw::setMMSBoundaryConditions(const double time)
{
  PetscErrorCode ierr = 0;
  string funcName = "PowerLaw::setMMSBoundaryConditions";
  string fileName = "maxwellViscoelastic.cpp";
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),fileName.c_str());CHKERRQ(ierr);
  #endif

  // set up boundary conditions: L and R
  PetscScalar y,z,v;
  PetscInt Ii,Istart,Iend;
  ierr = VecGetOwnershipRange(_bcLP,&Istart,&Iend);CHKERRQ(ierr);
  if (_Nz == 1) {
    Ii = Istart;
    y = 0;
    if (!_bcLType.compare("Dirichlet")) { v = MMS_uA1D(y,time); } // uAnal(y=0,z)
    else if (!_bcLType.compare("Neumann")) { v = MMS_mu1D(y) * (MMS_uA_y1D(y,time)); } // sigma_xy = mu * d/dy u
    ierr = VecSetValues(_bcLP,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);

    y = _Ly;
    if (!_bcRType.compare("Dirichlet")) { v = MMS_uA1D(y,time); } // uAnal(y=Ly,z)
    else if (!_bcRType.compare("Neumann")) { v = MMS_mu1D(y) * (MMS_uA_y1D(y,time)); } // sigma_xy = mu * d/dy u
    ierr = VecSetValues(_bcRP,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
  }
  else {
    for(Ii=Istart;Ii<Iend;Ii++) {
      //~ z = _dz * Ii;
      ierr = VecGetValues(*_z,1,&Ii,&z);CHKERRQ(ierr);

      y = 0;
      if (!_bcLType.compare("Dirichlet")) { v = MMS_uA(y,z,time); } // uAnal(y=0,z)
      else if (!_bcLType.compare("Neumann")) { v = MMS_mu(y,z) * (MMS_uA_y(y,z,time)- MMS_gxy(y,z,time));} // sigma_xy = mu * d/dy u
      ierr = VecSetValues(_bcLP,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);

      y = _Ly;
      if (!_bcRType.compare("Dirichlet")) { v = MMS_uA(y,z,time); } // uAnal(y=Ly,z)
      else if (!_bcRType.compare("Neumann")) { v = MMS_mu(y,z) * (MMS_uA_y(y,z,time)- MMS_gxy(y,z,time)); } // sigma_xy = mu * d/dy u
      ierr = VecSetValues(_bcRP,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(_bcLP);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_bcRP);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcLP);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcRP);CHKERRQ(ierr);


  // set up boundary conditions: T and B
  ierr = VecGetOwnershipRange(*_y,&Istart,&Iend);CHKERRQ(ierr);
  for(Ii=Istart;Ii<Iend;Ii++) {
    if (Ii % _Nz == 0) {
    //~ y = _dy * Ii;
    ierr = VecGetValues(*_y,1,&Ii,&y);CHKERRQ(ierr);
    PetscInt Jj = Ii / _Nz;

    z = 0;
    if (!_bcTType.compare("Dirichlet")) { v = MMS_uA(y,z,time); } // uAnal(y,z=0)
    else if (!_bcTType.compare("Neumann")) { v = MMS_mu(y,z) * (MMS_uA_z(y,z,time) - MMS_gxz(y,z,time)); }
    //~ else if (!_bcTType.compare("Neumann")) { v = MMS_mu(y,z) * (MMS_uA_z(y,z,time)); }
    ierr = VecSetValues(_bcTP,1,&Jj,&v,INSERT_VALUES);CHKERRQ(ierr);

    z = _Lz;
    if (!_bcBType.compare("Dirichlet")) { v = MMS_uA(y,z,time); } // uAnal(y,z=Lz)
    else if (!_bcBType.compare("Neumann")) { v = MMS_mu(y,z) * (MMS_uA_z(y,z,time) - MMS_gxz(y,z,time));}
    //~ else if (!_bcBType.compare("Neumann")) { v = MMS_mu(y,z) * (MMS_uA_z(y,z,time)); }
    ierr = VecSetValues(_bcBP,1,&Jj,&v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(_bcTP); CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_bcBP); CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcTP); CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcBP); CHKERRQ(ierr);

  #if VERBOSE > 1
    PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s.\n",funcName.c_str(),fileName.c_str());
  #endif
  return ierr;
}





PetscErrorCode PowerLaw::timeMonitor(const PetscReal time,const PetscInt stepCount,
      const map<string,Vec>& varEx,const map<string,Vec>& dvarEx)
{
  PetscErrorCode ierr = 0;

  _stepCount = stepCount;
  _currTime = time;
  ierr = setSurfDisp();

  if ( stepCount % _stride1D == 0) {
    //~ierr = PetscViewerHDF5IncrementTimestep(D->viewer);CHKERRQ(ierr);
    ierr = writeStep1D();CHKERRQ(ierr);
  }

  if ( stepCount % _stride2D == 0) {
    //~ierr = PetscViewerHDF5IncrementTimestep(D->viewer);CHKERRQ(ierr);
    ierr = writeStep2D();CHKERRQ(ierr);
  }

  if (stepCount % 50 == 0) {
    PetscScalar maxTimeStep_tot, maxDeltaT_Tmax = 0.0;
    computeMaxTimeStep(maxDeltaT_Tmax);
    maxTimeStep_tot = min(_maxDeltaT,maxDeltaT_Tmax);
    if (_timeIntegrator.compare("IMEX")==0) {
        _quadImex->setTimeStepBounds(_minDeltaT,maxTimeStep_tot);CHKERRQ(ierr);
    }
    else { _quadEx->setTimeStepBounds(_minDeltaT,maxTimeStep_tot);CHKERRQ(ierr); }
  }


#if VERBOSE > 0
  ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %.15e\n",stepCount,_currTime);CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode PowerLaw::timeMonitor(const PetscReal time,const PetscInt stepCount)
{
  PetscErrorCode ierr = 0;

  _stepCount = stepCount;
  _currTime = time;
  ierr = setSurfDisp();

  if ( stepCount % _stride1D == 0) {
    //~ierr = PetscViewerHDF5IncrementTimestep(D->viewer);CHKERRQ(ierr);
    ierr = writeStep1D();CHKERRQ(ierr);
  }

  if ( stepCount % _stride2D == 0) {
    //~ierr = PetscViewerHDF5IncrementTimestep(D->viewer);CHKERRQ(ierr);
    ierr = writeStep2D();CHKERRQ(ierr);
  }


#if VERBOSE > 0
  ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %.15e\n",stepCount,_currTime);CHKERRQ(ierr);
#endif
  return ierr;
}

// Outputs data at each time step.
PetscErrorCode PowerLaw::debug(const PetscReal time,const PetscInt stepCount,
                     const_it_vec varBegin,const_it_vec dvarBegin,const char *stage)
{
  PetscErrorCode ierr = 0;
/*
#if ODEPRINT > 0
  PetscInt       Istart,Iend;
  PetscScalar    bcRval,uVal,psiVal,velVal,dQVal,tauQS;
  PetscScalar    epsVxy,depsVxy;

  ierr= VecGetOwnershipRange(*varBegin,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(*varBegin,1,&Istart,&psiVal);CHKERRQ(ierr);

  ierr = VecGetValues(*(varBegin+1),1,&Istart,&uVal);CHKERRQ(ierr);

  ierr= VecGetOwnershipRange(*dvarBegin,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(*dvarBegin,1,&Istart,&dQVal);CHKERRQ(ierr);
  ierr = VecGetValues(*(dvarBegin+1),1,&Istart,&velVal);CHKERRQ(ierr);

  ierr = VecGetValues(*(varBegin+2),1,&Istart,&epsVxy);CHKERRQ(ierr);
  ierr = VecGetValues(*(dvarBegin+2),1,&Istart,&depsVxy);CHKERRQ(ierr);

  ierr= VecGetOwnershipRange(_bcRP,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(_bcRP,1,&Istart,&bcRval);CHKERRQ(ierr);

  ierr = VecGetValues(_fault._tauQSP,1,&Istart,&tauQS);CHKERRQ(ierr);

  if (stepCount == 0) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"%-4s %-6s  | %-15s %-15s %-15s | %-15s %-15s %-16s | %-15s\n",
                       "Step","Stage", "bcR","D","eVxy", "tauQS","V","deVxy","time");
    CHKERRQ(ierr);
  }
  ierr = PetscPrintf(PETSC_COMM_WORLD,"%4i %-6s ",stepCount,stage);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD," | %.9e %.9e %.9e ",bcRval,uVal,epsVxy);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD," | %.9e %.9e %.9e ",tauQS,velVal,depsVxy);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD," | %.9e\n",time);CHKERRQ(ierr);


  //~VecView(_fault._tauQSP,PETSC_VIEWER_STDOUT_WORLD);
#endif
*/
  return ierr;
}

PetscErrorCode PowerLaw::measureMMSError()
{
  PetscErrorCode ierr = 0;

  // measure error between analytical and numerical solution
  Vec uA,gxyA,gxzA;
  VecDuplicate(_uP,&uA);
  VecDuplicate(_uP,&gxyA);
  VecDuplicate(_uP,&gxzA);

  if (_Nz == 1) { mapToVec(uA,MMS_uA1D,*_y,_currTime); }
  else { mapToVec(uA,MMS_uA,*_y,*_z,_currTime); }
    if (_Nz == 1) { mapToVec(gxyA,MMS_gxy1D,*_y,_currTime); }
  else { mapToVec(gxyA,MMS_gxy,*_y,*_z,_currTime); }
  if (_Nz == 1) { mapToVec(gxzA,MMS_gxy1D,*_y,_currTime); }
  else { mapToVec(gxzA,MMS_gxz,*_y,*_z,_currTime); }

  writeVec(uA,_outputDir+"mms_uA");
  writeVec(gxyA,_outputDir+"mms_gxyA");
  writeVec(gxzA,_outputDir+"mms_gxzA");
  writeVec(_bcLP,_outputDir+"mms_bcL");
  writeVec(_bcRP,_outputDir+"mms_bcR");
  writeVec(_bcTP,_outputDir+"mms_bcT");
  writeVec(_bcBP,_outputDir+"mms_bcB");

  double err2u = computeNormDiff_2(_uP,uA);
  double err2epsxy = computeNormDiff_2(_varEx["gVxy"],gxyA);
  double err2epsxz = computeNormDiff_2(_varEx["gVxz"],gxzA);

  PetscPrintf(PETSC_COMM_WORLD,"%i %3i %.4e %.4e % .15e %.4e % .15e %.4e % .15e\n",
              _order,_Ny,_dy,err2u,log2(err2u),err2epsxy,log2(err2epsxy),err2epsxz,log2(err2epsxz));

  VecDestroy(&uA);
  VecDestroy(&gxyA);
  VecDestroy(&gxzA);
  return ierr;
}



//======================================================================
// IO functions
//======================================================================

// Save all scalar fields to text file named pl_domain.txt in output directory.
// Note that only the rank 0 processor's values will be saved.
PetscErrorCode PowerLaw::writeDomain()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::writeDomain";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  // output scalar fields
  std::string str = _outputDir + "pl_context.txt";
  PetscViewer    viewer;

  PetscViewerCreate(PETSC_COMM_WORLD, &viewer);
  PetscViewerSetType(viewer, PETSCVIEWERASCII);
  PetscViewerFileSetMode(viewer, FILE_MODE_WRITE);
  PetscViewerFileSetName(viewer, str.c_str());

  ierr = PetscViewerASCIIPrintf(viewer,"viscDistribution = %s\n",_viscDistribution.c_str());CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"thermalCoupling = %s\n",_thermalCoupling.c_str());CHKERRQ(ierr);

  PetscMPIInt size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  ierr = PetscViewerASCIIPrintf(viewer,"numProcessors = %i\n",size);CHKERRQ(ierr);

  PetscViewerDestroy(&viewer);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

PetscErrorCode PowerLaw::writeContext()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::writeContext";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif

  writeDomain();

  PetscViewer    vw;

  std::string str = _outputDir + "powerLawA";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&vw);CHKERRQ(ierr);
  ierr = VecView(_A,vw);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&vw);CHKERRQ(ierr);

  str = _outputDir + "powerLawB";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&vw);CHKERRQ(ierr);
  ierr = VecView(_B,vw);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&vw);CHKERRQ(ierr);

  str = _outputDir + "n";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&vw);CHKERRQ(ierr);
  ierr = VecView(_n,vw);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&vw);CHKERRQ(ierr);

  // contextual fields of members
  ierr = _sbpP->writeOps(_outputDir + "ops_u_"); CHKERRQ(ierr);
  ierr = _fault.writeContext(_outputDir); CHKERRQ(ierr);
  ierr = _he.writeContext(); CHKERRQ(ierr);

  // write out scalar info
  PetscViewer viewer;
  str = _outputDir + "powerLaw_context.txt";
  PetscViewerCreate(PETSC_COMM_WORLD, &viewer);
  PetscViewerSetType(viewer, PETSCVIEWERASCII);
  PetscViewerFileSetMode(viewer, FILE_MODE_WRITE);
  PetscViewerFileSetName(viewer, str.c_str());

  ierr = PetscViewerASCIIPrintf(viewer,"SAT term set to 0\n");CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"Imposing SS on state variable\n");CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),FILENAME);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


PetscErrorCode PowerLaw::writeStep1D()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::writeStep1D";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s at step %i\n",funcName.c_str(),FILENAME,_stepCount);
    CHKERRQ(ierr);
  #endif

  double startTime = MPI_Wtime();

  if (_stepCount==0) {
    // write contextual fields

    //~ierr = _sbpP->writeOps(_outputDir);CHKERRQ(ierr);
    ierr = writeContext();CHKERRQ(ierr);


    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(_outputDir+"time.txt").c_str(),&_timeV1D);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_timeV1D, "%.15e\n",_currTime);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"surfDispPlus").c_str(),
                                 FILE_MODE_WRITE,&_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = VecView(_surfDispPlus,_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"surfDispPlus").c_str(),
                                   FILE_MODE_APPEND,&_surfDispPlusViewer);CHKERRQ(ierr);

    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcR").c_str(),
              //~FILE_MODE_WRITE,&_bcRPlusV);CHKERRQ(ierr);
    //~ierr = VecView(_bcRP,_bcRPlusV);CHKERRQ(ierr);
    //~ierr = PetscViewerDestroy(&_bcRPlusV);CHKERRQ(ierr);
    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcR").c_str(),
                                   //~FILE_MODE_APPEND,&_bcRPlusV);CHKERRQ(ierr);

    ierr = _fault.writeStep(_outputDir,_stepCount);CHKERRQ(ierr);
    ierr = _he.writeStep1D(_stepCount);CHKERRQ(ierr);
  }
  else {
    ierr = PetscViewerASCIIPrintf(_timeV1D, "%.15e\n",_currTime);CHKERRQ(ierr);
    ierr = _fault.writeStep(_outputDir,_stepCount);CHKERRQ(ierr);
    ierr = _he.writeStep1D(_stepCount);CHKERRQ(ierr);

    ierr = VecView(_surfDispPlus,_surfDispPlusViewer);CHKERRQ(ierr);
    //~ierr = VecView(_bcRP,_bcRPlusV);CHKERRQ(ierr);
  }

  _writeTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s at step %i\n",funcName.c_str(),FILENAME,_stepCount);
    CHKERRQ(ierr);
  #endif
  return ierr;
}


PetscErrorCode PowerLaw::writeStep2D()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    string funcName = "PowerLaw::writeStep2D";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s at step %i\n",funcName.c_str(),FILENAME,_stepCount);
    CHKERRQ(ierr);
  #endif

  double startTime = MPI_Wtime();

  if (_stepCount==0) {
    _he.writeStep2D(_stepCount);

    // write contextual fields
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(_outputDir+"time2D.txt").c_str(),&_timeV2D);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(_timeV2D, "%.15e\n",_currTime);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"u").c_str(),
              FILE_MODE_WRITE,&_uPV);CHKERRQ(ierr);
    ierr = VecView(_uP,_uPV);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_uPV);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"u").c_str(),
                                   FILE_MODE_APPEND,&_uPV);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"gTxyP").c_str(),
              FILE_MODE_WRITE,&_gTxyPV);CHKERRQ(ierr);
    ierr = VecView(_gTxyP,_gTxyPV);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_gTxyPV);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"gTxyP").c_str(),
                                   FILE_MODE_APPEND,&_gTxyPV);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"stressxyP").c_str(),
              FILE_MODE_WRITE,&_sxyPV);CHKERRQ(ierr);
    ierr = VecView(_sxyP,_sxyPV);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_sxyPV);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"stressxyP").c_str(),
                                   FILE_MODE_APPEND,&_sxyPV);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"gxyP").c_str(),
              FILE_MODE_WRITE,&_gxyPV);CHKERRQ(ierr);
    ierr = VecView(_gxyP,_gxyPV);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_gxyPV);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"gxyP").c_str(),
                                   FILE_MODE_APPEND,&_gxyPV);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"effVisc").c_str(),
              FILE_MODE_WRITE,&_effViscV);CHKERRQ(ierr);
    ierr = VecView(_effVisc,_effViscV);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_effViscV);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"effVisc").c_str(),
                                   FILE_MODE_APPEND,&_effViscV);CHKERRQ(ierr);

    // write out boundary conditions for testing purposes
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcR").c_str(),
              FILE_MODE_WRITE,&_bcRPlusV);CHKERRQ(ierr);
    ierr = VecView(_bcRP,_bcRPlusV);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_bcRPlusV);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcR").c_str(),
                                   FILE_MODE_APPEND,&_bcRPlusV);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcL").c_str(),
              FILE_MODE_WRITE,&_bcLPlusV);CHKERRQ(ierr);
    ierr = VecView(_bcLP,_bcLPlusV);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_bcLPlusV);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcL").c_str(),
                                   FILE_MODE_APPEND,&_bcLPlusV);CHKERRQ(ierr);

    //~if (_isMMS) {
      //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"uAnal").c_str(),
                //~FILE_MODE_WRITE,&_uAnalV);CHKERRQ(ierr);
      //~ierr = VecView(_uAnal,_uAnalV);CHKERRQ(ierr);
      //~ierr = PetscViewerDestroy(&_uAnalV);CHKERRQ(ierr);
      //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"uAnal").c_str(),
                                     //~FILE_MODE_APPEND,&_uAnalV);CHKERRQ(ierr);
    //~}
    if (_Nz>1)
    {
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"gTxzP").c_str(),
              FILE_MODE_WRITE,&_gTxzPV);CHKERRQ(ierr);
      ierr = VecView(_gTxzP,_gTxzPV);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_gTxzPV);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"gTxzP").c_str(),
                                     FILE_MODE_APPEND,&_gTxzPV);CHKERRQ(ierr);

      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"stressxzP").c_str(),
               FILE_MODE_WRITE,&_sxzPV);CHKERRQ(ierr);
      ierr = VecView(_sxzP,_sxzPV);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_sxzPV);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"stressxzP").c_str(),
                                     FILE_MODE_APPEND,&_sxzPV);CHKERRQ(ierr);

      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"gxzP").c_str(),
               FILE_MODE_WRITE,&_gxzPV);CHKERRQ(ierr);
      ierr = VecView(_gxzP,_gxzPV);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_gxzPV);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"gxzP").c_str(),
                                   FILE_MODE_APPEND,&_gxzPV);CHKERRQ(ierr);
    }
  }
  else {
    ierr = PetscViewerASCIIPrintf(_timeV2D, "%.15e\n",_currTime);CHKERRQ(ierr);
    _he.writeStep2D(_stepCount);

    ierr = VecView(_bcRP,_bcRPlusV);CHKERRQ(ierr);
    ierr = VecView(_bcLP,_bcLPlusV);CHKERRQ(ierr);

    ierr = VecView(_uP,_uPV);CHKERRQ(ierr);
    ierr = VecView(_gTxyP,_gTxyPV);CHKERRQ(ierr);
    ierr = VecView(_sxyP,_sxyPV);CHKERRQ(ierr);
    ierr = VecView(_gxyP,_gxyPV);CHKERRQ(ierr);
    ierr = VecView(_effVisc,_effViscV);CHKERRQ(ierr);
    //~if (_isMMS) {ierr = VecView(_uAnal,_uAnalV);CHKERRQ(ierr);}
    if (_Nz>1)
    {
      ierr = VecView(_gTxzP,_gTxzPV);CHKERRQ(ierr);
      ierr = VecView(_sxzP,_sxzPV);CHKERRQ(ierr);
      ierr = VecView(_gxzP,_gxzPV);CHKERRQ(ierr);
    }
  }

  _writeTime += MPI_Wtime() - startTime;
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s at step %i\n",funcName.c_str(),FILENAME,_stepCount);
    CHKERRQ(ierr);
  #endif
  return ierr;
}

PetscErrorCode PowerLaw::view()
{
  PetscErrorCode ierr = 0;
  if (_timeIntegrator.compare("IMEX")==0) { ierr = _quadImex->view(); _he.view(); }
  if (_timeIntegrator.compare("RK32")==0) { ierr = _quadEx->view(); }

  ierr = PetscPrintf(PETSC_COMM_WORLD,"-------------------------------\n\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Power Law Runtime Summary:\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   Ny = %i, Nz = %i\n",_Ny,_Nz);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   solver algorithm = %s\n",_linSolver.c_str());CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent in integration (s): %g\n",_integrateTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent writing output (s): %g\n",_writeTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   number of times linear system was solved: %i\n",_linSolveCount);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent solving linear system (s): %g\n",_linSolveTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   %% integration time spent solving linear system: %g\n",_linSolveTime/_integrateTime*100.);CHKERRQ(ierr);

  //~ ierr = PetscPrintf(PETSC_COMM_WORLD,"   misc time (s): %g\n",_miscTime);CHKERRQ(ierr);
  //~ ierr = PetscPrintf(PETSC_COMM_WORLD,"   %% misc time: %g\n",_miscTime/_integrateTime*100.);CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"\n");CHKERRQ(ierr);
  return ierr;
}







// why not use the genFuncs implementation??
// Fills vec with the linear interpolation between the pairs of points (vals,depths)
PetscErrorCode PowerLaw::setVecFromVectors(Vec& vec, vector<double>& vals,vector<double>& depths)
{
  PetscErrorCode ierr = 0;
  PetscInt       Istart,Iend;
  PetscScalar    v,z,z0,z1,v0,v1;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::setVecFromVectors";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),fileName.c_str());
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
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}



// Play around with psuedo-timestepping
PetscErrorCode PowerLaw::psuedoTS_main()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::psuedoTS";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif

  VecSet(_effVisc,1e11);

  // compute mu*effVisc^(-1) for Jacobian
  Vec muDivVisc;
  ierr = VecCreate(PETSC_COMM_WORLD,&muDivVisc);CHKERRQ(ierr);
  ierr = VecSetSizes(muDivVisc,PETSC_DECIDE,2*_Ny*_Nz);CHKERRQ(ierr);
  ierr = VecSetFromOptions(muDivVisc);CHKERRQ(ierr);
  VecSet(muDivVisc,0.0);
  Vec temp; VecDuplicate(_muVecP,&temp);
  VecPointwiseDivide(temp, _muVecP, _effVisc);
  repVec(muDivVisc,temp, 2);
  VecDestroy(&temp);


  // create Jacobian
  Mat J;
  MatCreate(PETSC_COMM_WORLD,&J);
  MatSetSizes(J,PETSC_DECIDE,PETSC_DECIDE,2*_Ny*_Nz,2*_Ny*_Nz);
  MatSetFromOptions(J);
  MatMPIAIJSetPreallocation(J,1,NULL,0,NULL); // nnz per row
  MatSeqAIJSetPreallocation(J,1,NULL); // nnz per row
  MatSetUp(J);
  MatDiagonalSet(J,muDivVisc,INSERT_VALUES);

  // create Vec to contain output
  Vec g;
  VecDuplicate(muDivVisc,&g);
  VecSet(g,0.);
  VecDestroy(&muDivVisc);

  // create time stepper context
  TS ts;
  TSCreate(PETSC_COMM_WORLD,&ts);
  TSSetProblemType(ts,TS_NONLINEAR);
  TSSetSolution(ts,g); // where to compute solution
  TSSetInitialTimeStep(ts,0.0,1e-3); // set initial time (meaningless), and time step
  TSPseudoSetTimeStep(ts,TSPseudoTimeStepDefault,0); // strategy for increasing time step
  TSSetDuration(ts,1e5,1e10); // # of timesteps and final time
  TSSetExactFinalTime(ts,TS_EXACTFINALTIME_STEPOVER);

  // provide call-back functions
  void* ctx = this;
  TSSetIJacobian(ts,J,J,computeIJacobian,ctx);
  TSSetIFunction(ts,NULL,evaluateIRHS,ctx);
  //~ TSSetRHSJacobian(ts,J,J,computeJacobian,ctx);
  //~ TSSetRHSFunction(ts,NULL,evaluateRHS,ctx);
  TSMonitorSet(ts,monitor,ctx,NULL);


  TSSetFromOptions(ts);
  TSSetUp(ts);

  //~ TSGetTolerances(TS ts,PetscReal *atol,Vec *vatol,PetscReal *rtol,Vec *vrtol)
  //~ PetscReal atol, rtol;
  //~ TSGetTolerances(ts,&atol,NULL,&rtol,NULL);
  //~ PetscPrintf(PETSC_COMM_WORLD,"atol = %g, %rtol = %g\n",atol,rtol);


  TSSolve(ts,g);


  /*
  Vec gxy, gxz, g;
  VecDuplicate(mu,&g);
  PetscInt Istart,Iend;
  VecGetOwnershipRange(g,&Istart,&Iend);
  for( PetscInt Ii=Istart; Ii<Iend; Ii++) {
    PetscScalar v = Ii;
    VecSetValue(g,Ii,v,INSERT_VALUES);
  }
  ierr = VecAssemblyBegin(g);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(g);CHKERRQ(ierr);

  VecDuplicate(_muVecP,&gxy); VecSet(gxy,0.0);
  VecDuplicate(_muVecP,&gxz); VecSet(gxz,0.0);
  sepVec(gxy,g,0,_Ny*_Nz);
  sepVec(gxz,g,_Ny*_Nz,2*_Ny*_Nz);
  VecSet(g,0.0);
  distributeVec(g,gxy,0,_Ny*_Nz);
  distributeVec(g,gxz,_Ny*_Nz,2*_Ny*_Nz);
  VecView(g,PETSC_VIEWER_STDOUT_WORLD);
  */




  //~ VecDestroy(&mu);
  //~ VecDestroy(&effVisc);
  //~ VecDestroy(&muDivVisc);
  MatDestroy(&J);

  PetscPrintf(PETSC_COMM_WORLD,"hello world!\n");
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// returns F(X,Xdot)
PetscErrorCode PowerLaw::psuedoTS_evaluateIRHS(Vec&F,PetscReal time,Vec& g,Vec& g_t)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::psuedoTS_evaluateIRHS";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s at time %f\n",funcName.c_str(),fileName.c_str(),time);
    CHKERRQ(ierr);
  #endif


  // extract gxy and gxz from g
  sepVec(_gxyP,g,0,_Ny*_Nz);
  sepVec(_gxzP,g,_Ny*_Nz,2*_Ny*_Nz);

  // extract _gxy_t and _gxz_t from g_t
  Vec _gxy_t, _gxz_t;
  VecDuplicate(_gxyP,&_gxy_t); VecSet(_gxy_t,0.0);
  VecDuplicate(_gxzP,&_gxz_t); VecSet(_gxz_t,0.0);
  sepVec(_gxy_t,g_t,0,_Ny*_Nz);
  sepVec(_gxz_t,g_t,_Ny*_Nz,2*_Ny*_Nz);


  // solve for u
  // add source terms to rhs: d/dy( 2*mu*gVxy ) + d/dz( 2*mu*gVxz )
  Vec viscSource;
  ierr = VecDuplicate(_gxyP,&viscSource);CHKERRQ(ierr);
  ierr = VecSet(viscSource,0.0);CHKERRQ(ierr);
  ierr = setViscStrainSourceTerms(viscSource,_gxyP,_gxzP);CHKERRQ(ierr);

  // set up rhs vector
  //~ ierr = VecSet(_bcRP,_vL*time/2.0);CHKERRQ(ierr);
  //~ ierr = VecAXPY(_bcRP,1.0,_bcRPShift);CHKERRQ(ierr);
  ierr = _sbpP->setRhs(_rhsP,_bcLP,_bcRP,_bcTP,_bcBP);CHKERRQ(ierr); // update rhs from BCs
  ierr = VecAXPY(_rhsP,1.0,viscSource);CHKERRQ(ierr);


  // solve for displacement u
  double startTime = MPI_Wtime();
  ierr = KSPSolve(_kspP,_rhsP,_uP);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;


  // solve for u_t
  Vec bcL;
  VecDuplicate(_bcRP,&bcL);
  VecSet(bcL,0.0);
  //~ VecSet(_bcRP,_vL/2.0);
  VecSet(_bcRP,0);
  ierr = _sbpP->setRhs(_rhsP,bcL,_bcRP,_bcTP,_bcBP);CHKERRQ(ierr); // update rhs from BCs
  ierr = setViscStrainSourceTerms(viscSource,_gxy_t,_gxz_t);CHKERRQ(ierr);
  ierr = VecAXPY(_rhsP,1.0,viscSource);CHKERRQ(ierr);

  // solve for u_t
  Vec u_t;
  VecDuplicate(_uP,&u_t);
  startTime = MPI_Wtime();
  ierr = KSPSolve(_kspP,_rhsP,u_t);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;
  VecDestroy(&viscSource);
  VecDestroy(&bcL);


  // evaluate RHS
  ierr = setStresses(time);CHKERRQ(ierr); // also computes gTxy, gTxz
  //~ computeViscosity();
  VecSet(_effVisc,1e11);
  Vec _gTxy_t, _gTxz_t;
  VecDuplicate(_gxyP,&_gTxy_t); VecSet(_gTxy_t,0.0);
  VecDuplicate(_gxyP,&_gTxz_t); VecSet(_gTxz_t,0.0);
  _sbpP->Dy(_uP,_gTxyP);
  _sbpP->Dz(_uP,_gTxzP);

  Vec _gExy_t, _gExz_t;
  VecDuplicate(_gxyP,&_gExy_t); VecSet(_gExy_t,0.0);
  VecDuplicate(_gxzP,&_gExz_t); VecSet(_gExz_t,0.0);
  PetscInt Istart,Iend;
  PetscScalar mu,effVisc,gTxy_t,gTxy,gVxy,gExy_t = 0.;
  PetscScalar gTxz_t,gTxz,gVxz,gExz_t = 0.;
  VecGetOwnershipRange(_gExy_t,&Istart,&Iend);
  for( PetscInt Ii=Istart; Ii<Iend; Ii++) {
    ierr = VecGetValues(_muVecP,1,&Ii,&mu);CHKERRQ(ierr);
    ierr = VecGetValues(_effVisc,1,&Ii,&effVisc);CHKERRQ(ierr);
    ierr = VecGetValues(_gTxyP,1,&Ii,&gTxy);CHKERRQ(ierr);
    ierr = VecGetValues(_gTxzP,1,&Ii,&gTxz);CHKERRQ(ierr);
    ierr = VecGetValues(_gxyP,1,&Ii,&gVxy);CHKERRQ(ierr);
    ierr = VecGetValues(_gxzP,1,&Ii,&gVxz);CHKERRQ(ierr);
    ierr = VecGetValues(_gTxy_t,1,&Ii,&gTxy_t);CHKERRQ(ierr);
    ierr = VecGetValues(_gTxz_t,1,&Ii,&gTxz_t);CHKERRQ(ierr);

    gExy_t = gTxy_t - mu/effVisc*(gTxy - gVxy);
    gExz_t = gTxz_t - mu/effVisc*(gTxz - gVxz);

    VecSetValue(_gExy_t,Ii,gExy_t,INSERT_VALUES);
    VecSetValue(_gExz_t,Ii,gExz_t,INSERT_VALUES);
  }
  ierr = VecAssemblyBegin(_gExy_t);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_gExz_t);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_gExy_t);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_gExz_t);CHKERRQ(ierr);

  // place elastic strain rates into output vector
  distributeVec(F,_gExy_t,0,_Ny*_Nz);
  distributeVec(F,_gExz_t,_Ny*_Nz,2*_Ny*_Nz);


  VecDestroy(&_gxy_t);
  VecDestroy(&_gxz_t);
  VecDestroy(&u_t);
  VecDestroy(&_gTxy_t);
  VecDestroy(&_gTxz_t);
  VecDestroy(&_gExy_t);
  VecDestroy(&_gExz_t);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// returns F(X,Xdot)
PetscErrorCode PowerLaw::psuedoTS_evaluateRHS(Vec& F,PetscReal time,Vec& g)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::psuedoTS_evaluateRHS";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s at time %f\n",funcName.c_str(),fileName.c_str(),time);
    CHKERRQ(ierr);
  #endif


  // extract gxy and gxz from g
  sepVec(_gxyP,g,0,_Ny*_Nz);
  sepVec(_gxzP,g,_Ny*_Nz,2*_Ny*_Nz);

  // extract _gxy_t and _gxz_t from g_t
  Vec _gxy_t, _gxz_t;
  VecDuplicate(_gxyP,&_gxy_t); VecSet(_gxy_t,0.0);
  VecDuplicate(_gxzP,&_gxz_t); VecSet(_gxz_t,0.0);
  //~ sepVec(_gxy_t,g_t,0,_Ny*_Nz);
  //~ sepVec(_gxz_t,g_t,_Ny*_Nz,2*_Ny*_Nz);


  // solve for u
  // add source terms to rhs: d/dy( 2*mu*gVxy ) + d/dz( 2*mu*gVxz )
  Vec viscSource;
  ierr = VecDuplicate(_gxyP,&viscSource);CHKERRQ(ierr);
  ierr = VecSet(viscSource,0.0);CHKERRQ(ierr);
  ierr = setViscStrainSourceTerms(viscSource,_gxyP,_gxzP);CHKERRQ(ierr);

  // set up rhs vector
  ierr = VecSet(_bcRP,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_bcRP,1.0,_bcRPShift);CHKERRQ(ierr);
  ierr = _sbpP->setRhs(_rhsP,_bcLP,_bcRP,_bcTP,_bcBP);CHKERRQ(ierr); // update rhs from BCs
  ierr = VecAXPY(_rhsP,1.0,viscSource);CHKERRQ(ierr);


  // solve for displacement u
  double startTime = MPI_Wtime();
  ierr = KSPSolve(_kspP,_rhsP,_uP);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;


  // compute intermediate fields
  ierr = setStresses(time);CHKERRQ(ierr); // also computes gTxy, gTxz
  //~ computeViscosity();
  Vec _gTxy_t, _gTxz_t;
  VecDuplicate(_gxyP,&_gTxy_t); VecSet(_gTxy_t,0.0);
  VecDuplicate(_gxyP,&_gTxz_t); VecSet(_gTxz_t,0.0);
  _sbpP->Dy(_uP,_gTxyP);
  _sbpP->Dz(_uP,_gTxzP);

  // compute viscous strains


  // solve for u_t
  Vec bcL;
  VecDuplicate(_bcRP,&bcL);
  VecSet(bcL,0.0);
  VecSet(_bcRP,_vL/2.0);
  ierr = _sbpP->setRhs(_rhsP,bcL,_bcRP,_bcTP,_bcBP);CHKERRQ(ierr); // update rhs from BCs
  ierr = setViscStrainSourceTerms(viscSource,_gxy_t,_gxz_t);CHKERRQ(ierr);
  ierr = VecAXPY(_rhsP,1.0,viscSource);CHKERRQ(ierr);

  // solve for u_t
  Vec u_t;
  VecDuplicate(_uP,&u_t);
  startTime = MPI_Wtime();
  ierr = KSPSolve(_kspP,_rhsP,u_t);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;
  VecDestroy(&viscSource);
  VecDestroy(&bcL);




  // evaluate RHS
  Vec _gExy_t, _gExz_t;
  VecDuplicate(_gxyP,&_gExy_t); VecSet(_gExy_t,0.0);
  VecDuplicate(_gxzP,&_gExz_t); VecSet(_gExz_t,0.0);
  PetscInt Istart,Iend;
  PetscScalar mu,effVisc,gTxy_t,gTxy,gVxy,gExy_t = 0.;
  PetscScalar gTxz_t,gTxz,gVxz,gExz_t = 0.;
  VecGetOwnershipRange(_gExy_t,&Istart,&Iend);
  for( PetscInt Ii=Istart; Ii<Iend; Ii++) {
    ierr = VecGetValues(_muVecP,1,&Ii,&mu);CHKERRQ(ierr);
    ierr = VecGetValues(_effVisc,1,&Ii,&effVisc);CHKERRQ(ierr);
    ierr = VecGetValues(_gTxyP,1,&Ii,&gTxy);CHKERRQ(ierr);
    ierr = VecGetValues(_gTxzP,1,&Ii,&gTxz);CHKERRQ(ierr);
    ierr = VecGetValues(_gxyP,1,&Ii,&gVxy);CHKERRQ(ierr);
    ierr = VecGetValues(_gxzP,1,&Ii,&gVxz);CHKERRQ(ierr);
    ierr = VecGetValues(_gTxy_t,1,&Ii,&gTxy_t);CHKERRQ(ierr);
    ierr = VecGetValues(_gTxz_t,1,&Ii,&gTxz_t);CHKERRQ(ierr);

    gExy_t = gTxy_t - mu/effVisc*(gTxy - gVxy);
    gExz_t = gTxz_t - mu/effVisc*(gTxz - gVxz);

    VecSetValue(_gExy_t,Ii,gExy_t,INSERT_VALUES);
    VecSetValue(_gExz_t,Ii,gExz_t,INSERT_VALUES);
  }
  ierr = VecAssemblyBegin(_gExy_t);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_gExz_t);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_gExy_t);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_gExz_t);CHKERRQ(ierr);

  // place elastic strain rates into output vector
  distributeVec(F,_gExy_t,0,_Ny*_Nz);
  distributeVec(F,_gExz_t,_Ny*_Nz,2*_Ny*_Nz);


  VecDestroy(&_gxy_t);
  VecDestroy(&_gxz_t);
  VecDestroy(&u_t);
  VecDestroy(&_gTxy_t);
  VecDestroy(&_gTxz_t);
  VecDestroy(&_gExy_t);
  VecDestroy(&_gExz_t);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}


// returns Jacobian for explicit solve
PetscErrorCode PowerLaw::psuedoTS_computeJacobian(Mat& J,PetscReal time,Vec& g)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::psuedoTS_computeJacobian(Mat& J,PetscReal time,Vec g)";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s at time %f\n",funcName.c_str(),fileName.c_str(),time);
    CHKERRQ(ierr);
  #endif

  // for now assume effective viscosity is constant
/*
  // extract gxy and gxz from g
  sepVec(_gxyP,g,0,_Ny*_Nz);
  sepVec(_gxzP,g,_Ny*_Nz,2*_Ny*_Nz);

  // solve for u
  // add source terms to rhs: d/dy( 2*mu*gVxy ) + d/dz( 2*mu*gVxz )
  Vec viscSource;
  ierr = VecDuplicate(_gxyP,&viscSource);CHKERRQ(ierr);
  ierr = VecSet(viscSource,0.0);CHKERRQ(ierr);
  ierr = setViscStrainSourceTerms(viscSource,_gxyP,_gxzP);CHKERRQ(ierr);

  // set up rhs vector
  ierr = VecSet(_bcRP,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_bcRP,1.0,_bcRPShift);CHKERRQ(ierr);
  ierr = _sbpP->setRhs(_rhsP,_bcLP,_bcRP,_bcTP,_bcBP);CHKERRQ(ierr); // update rhs from BCs
  ierr = VecAXPY(_rhsP,1.0,viscSource);CHKERRQ(ierr);
  VecDestroy(&viscSource);


  // solve for displacement u
  double startTime = MPI_Wtime();
  ierr = KSPSolve(_kspP,_rhsP,_uP);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;

  // evaluate RHS
  ierr = setStresses(time);CHKERRQ(ierr);
  computeViscosity();
  * */


  Vec muDivVisc;
  VecDuplicate(g,&muDivVisc);
  Vec temp; VecDuplicate(_muVecP,&temp);
  VecPointwiseDivide(temp, _muVecP, _effVisc);
  repVec(muDivVisc,temp, 2);
  MatDiagonalSet(J,muDivVisc,INSERT_VALUES);


  VecDestroy(&temp);
  VecDestroy(&muDivVisc);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}


// returns Jacobian for implicit
PetscErrorCode PowerLaw::psuedoTS_computeIJacobian(Mat& J,PetscReal time,Vec& g,Vec& g_t)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "PowerLaw::psuedoTS_computeIJacobian";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s at time %f\n",funcName.c_str(),fileName.c_str(),time);
    CHKERRQ(ierr);
  #endif

/*
  // extract gxy and gxz from g
  sepVec(_gxyP,g,0,_Ny*_Nz);
  sepVec(_gxzP,g,_Ny*_Nz,2*_Ny*_Nz);


  // solve for u
  // add source terms to rhs: d/dy( 2*mu*gVxy ) + d/dz( 2*mu*gVxz )
  Vec viscSource;
  ierr = VecDuplicate(_gxyP,&viscSource);CHKERRQ(ierr);
  ierr = VecSet(viscSource,0.0);CHKERRQ(ierr);
  ierr = setViscStrainSourceTerms(viscSource,_gxyP,_gxzP);CHKERRQ(ierr);

  // set up rhs vector
  ierr = VecSet(_bcRP,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_bcRP,1.0,_bcRPShift);CHKERRQ(ierr);
  ierr = _sbpP->setRhs(_rhsP,_bcLP,_bcRP,_bcTP,_bcBP);CHKERRQ(ierr); // update rhs from BCs
  ierr = VecAXPY(_rhsP,1.0,viscSource);CHKERRQ(ierr);
  VecDestroy(&viscSource);


  // solve for displacement u
  double startTime = MPI_Wtime();
  ierr = KSPSolve(_kspP,_rhsP,_uP);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;

  // evaluate RHS
  ierr = setStresses(time);CHKERRQ(ierr);
  computeViscosity();
  */


  Vec muDivVisc;
  VecDuplicate(g,&muDivVisc);
  VecSet(muDivVisc,-30./1e11);
  //~ Vec temp; VecDuplicate(_muVecP,&temp);
  //~ VecPointwiseDivide(temp, _muVecP, _effVisc);
  //~ repVec(muDivVisc,temp, 2);
  MatDiagonalSet(J,muDivVisc,INSERT_VALUES);


  //~ VecDestroy(&temp);
  VecDestroy(&muDivVisc);

  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}


//======================================================================
//======================================================================

// call-back function that returns Jacobian for PETSc'c TSSetIJacobian
PetscErrorCode computeIJacobian(TS ts,PetscReal time,Vec g,Vec g_t,PetscReal a,Mat Amat,Mat Pmat,void *ptr)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "computeIJacobian";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  PowerLaw *pl = (PowerLaw*) ptr; // from PETSc tutorial
  //~ PowerLaw *pl = static_cast<PowerLaw*> (ptr); // from stack overflow

  pl->psuedoTS_computeIJacobian(Amat,time,g,g_t);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}


// call-back function that returns Jacobian for PETSc'c TSSetRHSJacobian
PetscErrorCode computeJacobian(TS ts,PetscReal time,Vec g,Mat Amat,Mat Pmat,void *ptr)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "computeJacobian";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  //~ PowerLaw *pl = (PowerLaw*) ptrTSPSU; // from PETSc tutorial
  PowerLaw *pl = static_cast<PowerLaw*> (ptr); // from stack overflow

  pl->psuedoTS_computeJacobian(Amat,time,g);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// call-back function that returns F(X,Xdot) for PETSc'c TSSetIFunction
PetscErrorCode evaluateIRHS(TS ts,PetscReal time,Vec g,Vec g_t,Vec F,void *ptr)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "evaluateIRHS";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  //~ PowerLaw *pl = (PowerLaw*) ptr; // from PETSc tutorial
  PowerLaw *pl = static_cast<PowerLaw*> (ptr); // from stack overflow

  pl->psuedoTS_evaluateIRHS(F,time,g,g_t);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// call-back function that returns F(X,Xdot) for PETSc'c TSSetRHSFunction
PetscErrorCode evaluateRHS(TS ts,PetscReal time,Vec g,Vec F,void *ptr)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "evaluateRHS";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  //~ PowerLaw *pl = (PowerLaw*) ptr; // from PETSc tutorial
  PowerLaw *pl = static_cast<PowerLaw*> (ptr); // from stack overflow

  pl->psuedoTS_evaluateRHS(F,time,g);


  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s\n",funcName.c_str(),fileName.c_str());
    CHKERRQ(ierr);
  #endif
  return ierr;
}

// call-back function that writes relevent data to memory
PetscErrorCode monitor(TS ts,PetscInt stepCount,PetscReal time,Vec g,void *ptr)
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    std::string funcName = "monitor";
    std::string fileName = "PowerLaw";
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting %s in %s at time %.15e\n",funcName.c_str(),fileName.c_str(),time);
    CHKERRQ(ierr);
  #endif
  //~ PowerLaw *pl = (PowerLaw*) ptr; // from PETSc tutorial
  PowerLaw *pl = static_cast<PowerLaw*> (ptr); // from stack overflow

  sepVec(pl->_gxyP,g,0,pl->_Ny*pl->_Nz);
  sepVec(pl->_gxzP,g,pl->_Ny*pl->_Nz,2*pl->_Ny*pl->_Nz);
  //~ pl->timeMonitor(time,stepCount);



  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending %s in %s at time %.15e\n",funcName.c_str(),fileName.c_str(),time);
    CHKERRQ(ierr);
  #endif
  return ierr;
}
