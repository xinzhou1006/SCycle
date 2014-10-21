#include "domain.hpp"

Domain::Domain(const char *file)
: _file(file),_outputDir("")
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting constructor in domain.cpp.\n");
#endif

  loadData(_file);

  _dy = _Ly/(_Ny-1.0);
  _dz = _Lz/(_Nz-1.0);
  _initDeltaT = _minDeltaT;
  _f0=0.6;
  _v0=1e-6;
  _vp=1e-9;

  _csIn = sqrt(_muIn/_rhoIn);
  _csOut = sqrt(_muOut/_rhoOut);


  MatCreate(PETSC_COMM_WORLD,&_mu);
  setFields();

   //~view(0);
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending constructor in domain.cpp.\n");
#endif
}

Domain::~Domain()
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting destructor in domain.cpp.\n");
#endif

  PetscFree(_muArr);
  PetscFree(_rhoArr);
  PetscFree(_csArr);

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending destructor in domain.cpp.\n");
#endif
}



PetscErrorCode Domain::loadData(const char *file)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting loadData in domain.cpp.\n");CHKERRQ(ierr);
#endif
  PetscMPIInt rank,size;
  MPI_Comm_size(PETSC_COMM_WORLD,&size);
  MPI_Comm_rank(PETSC_COMM_WORLD,&rank);

  if (rank==0) {
    ifstream infile( file );
    string line,var;
    string delim = " = ";
    size_t pos = 0;
    while (getline(infile, line))
    {
      istringstream iss(line);
      pos = line.find(delim); // find position of delimiter
      var = line.substr(0,pos);

      if (var=="order") { _order = atoi( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="Ny") { _Ny = atoi( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="Nz") { _Nz = atoi( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="Ly") { _Ly = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="Lz") { _Lz = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }

      if (var=="Dc") { _Dc = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }

      // sedimentary basin properties
      if (var=="bAbove") { _bAbove = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="bBelow") { _bBelow = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="muIn") { _muIn = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="muOut") { _muOut = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="rhoIn") { _rhoIn = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="rhoOut") { _rhoOut = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="depth") { _depth = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="width") { _width = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="seisDepth") { _seisDepth = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }

      if (var=="sigma_N") { _sigma_N_val = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }

      if (var=="strideLength") { _strideLength = (int) atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="maxStepCount") { _maxStepCount = (int) atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="initTime") { _initTime = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="maxTime") { _maxTime = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="minDeltaT") { _minDeltaT = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="maxDeltaT") {_maxDeltaT = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="initDeltaT") { _initDeltaT = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="atol") { _atol = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }

      if (var=="kspTol") {_kspTol = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }
      if (var=="rootTol") { _rootTol = atof( (line.substr(pos+delim.length(),line.npos)).c_str() ); }

      if (var=="outputDir") { _outputDir = line.substr(pos+delim.length(),line.npos); }
    }
    MPI_Bcast(&_order,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_Ny,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_Nz,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_Ly,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_Lz,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_Dc,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_bAbove,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_bBelow,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_muIn,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_muOut,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_rhoIn,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_rhoOut,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_depth,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_width,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_seisDepth,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_sigma_N_val,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_strideLength,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_maxStepCount,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_initTime,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_maxTime,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_minDeltaT,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_maxDeltaT,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_initDeltaT,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_atol,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_kspTol,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_rootTol,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_outputDir,1,MPI_CHAR,0,PETSC_COMM_WORLD);
  }
  else {
    MPI_Bcast(&_order,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_Ny,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_Nz,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_Ly,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_Lz,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_Dc,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_bAbove,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_bBelow,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_muIn,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_muOut,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_rhoIn,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_rhoOut,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_depth,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_width,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_seisDepth,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_sigma_N_val,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_strideLength,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_maxStepCount,1,MPI_INT,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_initTime,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_maxTime,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_minDeltaT,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_maxDeltaT,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_initDeltaT,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_atol,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_kspTol,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);
    MPI_Bcast(&_rootTol,1,MPI_DOUBLE,0,PETSC_COMM_WORLD);

    MPI_Bcast(&_outputDir,1,MPI_CHAR,0,PETSC_COMM_WORLD);
  }

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending loadData in domain.cpp.\n");CHKERRQ(ierr);
#endif
  return ierr;
}

PetscErrorCode Domain::view(PetscMPIInt rank)
{
  PetscErrorCode ierr = 0;
  PetscMPIInt localRank;
  MPI_Comm_rank(PETSC_COMM_WORLD,&localRank);
  if (localRank==rank) {
    #if VERBOSE > 1
      ierr = PetscPrintf(PETSC_COMM_SELF,"Starting view in domain.cpp.\n");CHKERRQ(ierr);
    #endif

    PetscPrintf(PETSC_COMM_SELF,"\n\nrank=%i in view\n",rank);
    ierr = PetscPrintf(PETSC_COMM_SELF,"order = %i\n",_order);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"Ny = %i\n",_Ny);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"Nz = %i\n",_Nz);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"Ly = %e\n",_Ly);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"Lz = %e\n",_Lz);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"dy = %.15e\n",_dy);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"dz = %.15e\n",_dz);CHKERRQ(ierr);

    ierr = PetscPrintf(PETSC_COMM_SELF,"Dc = %.15e\n",_Dc);CHKERRQ(ierr);

    // sedimentary basin properties
    ierr = PetscPrintf(PETSC_COMM_SELF,"bAbove = %f\n",_bAbove);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"bBelow = %f\n",_bBelow);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"muIn = %f\n",_muIn);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"muOut = %f\n",_muOut);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"rhoIn = %f\n",_rhoIn);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"rhoOut = %f\n",_rhoOut);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"depth = %f\n",_depth);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"width = %f\n",_width);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"seisDepth = %f\n",_seisDepth);CHKERRQ(ierr);

    ierr = PetscPrintf(PETSC_COMM_SELF,"sigma_N_val = %f\n",_sigma_N_val);CHKERRQ(ierr);

    // time monitering
    ierr = PetscPrintf(PETSC_COMM_SELF,"strideLength = %i\n",_strideLength);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"maxStepCount = %i\n",_maxStepCount);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"initTime = %.15e\n",_initTime);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"maxTime = %.15e\n",_maxTime);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"atol = %.15e\n",_atol);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"minDeltaT = %.15e\n",_minDeltaT);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"maxDeltaT = %.15e\n",_maxDeltaT);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"initDeltaT = %.15e\n",_initDeltaT);CHKERRQ(ierr);

    // tolerances for linear and nonlinear (for vel) solve
    ierr = PetscPrintf(PETSC_COMM_SELF,"kspTol = %.15e\n",_kspTol);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_SELF,"rootTol = %.15e\n",_rootTol);CHKERRQ(ierr);

    #if VERBOSE > 1
      ierr = PetscPrintf(PETSC_COMM_SELF,"Ending view in domain.cpp.\n");CHKERRQ(ierr);
    #endif
  }
  return ierr;
}

PetscErrorCode Domain::write()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting write in domain.cpp.\n");CHKERRQ(ierr);
#endif

  // output scalar fields
  std::string str = _outputDir + "domain.txt";
  PetscViewer    viewer;

  PetscViewerCreate(PETSC_COMM_WORLD, &viewer);
  PetscViewerSetType(viewer, PETSCVIEWERASCII);
  PetscViewerFileSetMode(viewer, FILE_MODE_WRITE);
  PetscViewerFileSetName(viewer, str.c_str());

  ierr = PetscViewerASCIIPrintf(viewer,"order = %i\n",_order);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"Ny = %i\n",_Ny);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"Nz = %i\n",_Nz);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"Ly = %g\n",_Ly);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"Lz = %g\n",_Lz);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"dy = %.15e\n",_dy);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"dz = %.15e\n",_dz);CHKERRQ(ierr);

  ierr = PetscViewerASCIIPrintf(viewer,"Dc = %15e\n",_Dc);CHKERRQ(ierr);

  // sedimentary basin properties
  ierr = PetscViewerASCIIPrintf(viewer,"bAbove = %.15e\n",_bAbove);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"bBelow = %.15e\n",_bBelow);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"muIn = %.15e\n",_muIn);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"muOut = %.15e\n",_muOut);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"rhoIn = %.15e\n",_rhoIn);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"rhoOut = %.15e\n",_rhoOut);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"depth = %.15e\n",_depth);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"width = %.15e\n",_width);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"seisDepth = %.15e\n",_seisDepth);CHKERRQ(ierr);

  ierr = PetscViewerASCIIPrintf(viewer,"sigma_N_val = %.15e\n",_sigma_N_val);CHKERRQ(ierr);

  // time integration settings
  ierr = PetscViewerASCIIPrintf(viewer,"strideLength = %i\n",_strideLength);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxStepCount = %i\n",_maxStepCount);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"initTime = %.15e\n",_initTime);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxTime = %.15e\n",_maxTime);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"minDeltaT = %.15e\n",_minDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"maxDeltaT = %.15e\n",_maxDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"initDeltaT = %.15e\n",_initDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"atol = %.15e\n",_atol);CHKERRQ(ierr);

  // tolerances for linear and nonlinear (for vel) solve
  ierr = PetscViewerASCIIPrintf(viewer,"kspTol = %g\n",_kspTol);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"rootTol = %g\n",_rootTol);CHKERRQ(ierr);

  ierr = PetscViewerASCIIPrintf(viewer,"f0 = %g\n",_f0);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"v0 = %e\n",_v0);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"vp = %e\n",_vp);CHKERRQ(ierr);


  PetscMPIInt size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  ierr = PetscViewerASCIIPrintf(viewer,"numProcessors = %i\n",size);CHKERRQ(ierr);

  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  // output shear modulus matrix
  str =  _outputDir + "mu";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = MatView(_mu,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending write in domain.cpp.\n");CHKERRQ(ierr);
#endif
  return ierr;
}





PetscErrorCode Domain::setFields()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting setFields in domain.cpp.\n");CHKERRQ(ierr);
#endif

  PetscInt       Ii;
  PetscScalar    v,y,z;

  Vec muVec;
  PetscInt *muInds;
  ierr = PetscMalloc(_Ny*_Nz*sizeof(PetscInt),&muInds);CHKERRQ(ierr);
  ierr = PetscMalloc(_Ny*_Nz*sizeof(PetscScalar),&_muArr);CHKERRQ(ierr);
  ierr = PetscMalloc(_Ny*_Nz*sizeof(PetscScalar),&_rhoArr);CHKERRQ(ierr);
  ierr = PetscMalloc(_Ny*_Nz*sizeof(PetscScalar),&_csArr);CHKERRQ(ierr);

  ierr = VecCreate(PETSC_COMM_WORLD,&muVec);CHKERRQ(ierr);
  ierr = VecSetSizes(muVec,PETSC_DECIDE,_Ny*_Nz);CHKERRQ(ierr);
  ierr = VecSetFromOptions(muVec);CHKERRQ(ierr);

  PetscScalar r = 0;
  PetscScalar rbar = 0.25*_width*_width;
  //~PetscScalar rw = 1+0.5*_width/_depth;
  PetscScalar rw = 1+0.25*_width*_width/_depth/_depth;
  for (Ii=0;Ii<_Ny*_Nz;Ii++) {
    z = _dz*(Ii-_Nz*(Ii/_Nz));
    y = _dy*(Ii/_Nz);
    r=y*y+(0.25*_width*_width/_depth/_depth)*z*z;

    v = 0.5*(_rhoOut-_rhoIn)*(tanh((double)(r-rbar)/rw)+1) + _rhoIn;
    _rhoArr[Ii] = v;

    v = 0.5*(_csOut-_csIn)*(tanh((double)(r-rbar)/rw)+1) + _csIn;
    _csArr[Ii] = v;

    v = 0.5*(_muOut-_muIn)*(tanh((double)(r-rbar)/rw)+1) + _muIn;
    _muArr[Ii] = v;
    muInds[Ii] = Ii;
  }

#if DEBUG > 0
  PetscPrintf(PETSC_COMM_WORLD,"!!Setting mu = diag(2:Ny*Nz+1).\n");
  for (Ii=0;Ii<_Ny*_Nz;Ii++) {
    z = _dz*(Ii-_Nz*(Ii/_Nz));
    y = _dy*(Ii/_Nz);
    r=y*y+(0.25*_width*_width/_depth/_depth)*z*z;

    v = 0.5*(_rhoOut-_rhoIn)*(tanh((double)(r-rbar)/rw)+1) + _rhoIn;
    _rhoArr[Ii] = v;

    v = 0.5*(_csOut-_csIn)*(tanh((double)(r-rbar)/rw)+1) + _csIn;
    _csArr[Ii] = v;

    //~v = 0.5*(_muOut-_muIn)*(tanh((double)(r-rbar)/rw)+1) + _muIn;
    _muArr[Ii] = Ii+2;
    //~_muArr[Ii] = 1.0;
    muInds[Ii] = Ii;
  }
#endif


  ierr = VecSetValues(muVec,_Ny*_Nz,muInds,_muArr,INSERT_VALUES);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(muVec);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(muVec);CHKERRQ(ierr);

  ierr = MatSetSizes(_mu,PETSC_DECIDE,PETSC_DECIDE,_Ny*_Nz,_Ny*_Nz);CHKERRQ(ierr);
  ierr = MatSetFromOptions(_mu);CHKERRQ(ierr);
  ierr = MatMPIAIJSetPreallocation(_mu,1,NULL,1,NULL);CHKERRQ(ierr);
  ierr = MatSeqAIJSetPreallocation(_mu,1,NULL);CHKERRQ(ierr);
  ierr = MatSetUp(_mu);CHKERRQ(ierr);
  ierr = MatDiagonalSet(_mu,muVec,INSERT_VALUES);CHKERRQ(ierr);
  //~ierr = MatView(_mu,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  VecDestroy(&muVec);
  PetscFree(muInds);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending setFields in domain.cpp.\n");CHKERRQ(ierr);
#endif
return ierr;
}
