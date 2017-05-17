#include <cstdio>

#include "AMReX_LevelBld.H"
#include <AMReX_ParmParse.H>
#include "Castro.H"
#include "Castro_F.H"
#include <Derive_F.H>
#ifdef RADIATION
# include "Radiation.H"
# include "RAD_F.H"
#endif

#include "AMReX_buildInfo.H"

using std::string;
using namespace amrex;

static Box the_same_box (const Box& b) { return b; }
static Box grow_box_by_one (const Box& b) { return amrex::grow(b,1); }

typedef StateDescriptor::BndryFunc BndryFunc;

//
// Components are:
//  Interior, Inflow, Outflow,  Symmetry,     SlipWall,     NoSlipWall
//
static int scalar_bc[] =
  {
    INT_DIR, EXT_DIR, FOEXTRAP, REFLECT_EVEN, REFLECT_EVEN, REFLECT_EVEN
  };

static int norm_vel_bc[] =
  {
    INT_DIR, EXT_DIR, FOEXTRAP, REFLECT_ODD,  REFLECT_ODD,  REFLECT_ODD
  };

static int tang_vel_bc[] =
  {
    INT_DIR, EXT_DIR, FOEXTRAP, REFLECT_EVEN, REFLECT_EVEN, REFLECT_EVEN
  };

static
void
set_scalar_bc (BCRec& bc, const BCRec& phys_bc)
{
  const int* lo_bc = phys_bc.lo();
  const int* hi_bc = phys_bc.hi();
  for (int i = 0; i < BL_SPACEDIM; i++)
    {
      bc.setLo(i,scalar_bc[lo_bc[i]]);
      bc.setHi(i,scalar_bc[hi_bc[i]]);
    }
}

static
void
set_x_vel_bc(BCRec& bc, const BCRec& phys_bc)
{
  const int* lo_bc = phys_bc.lo();
  const int* hi_bc = phys_bc.hi();
  bc.setLo(0,norm_vel_bc[lo_bc[0]]);
  bc.setHi(0,norm_vel_bc[hi_bc[0]]);
#if (BL_SPACEDIM >= 2)
  bc.setLo(1,tang_vel_bc[lo_bc[1]]);
  bc.setHi(1,tang_vel_bc[hi_bc[1]]);
#endif
#if (BL_SPACEDIM == 3)
  bc.setLo(2,tang_vel_bc[lo_bc[2]]);
  bc.setHi(2,tang_vel_bc[hi_bc[2]]);
#endif
}

static
void
set_y_vel_bc(BCRec& bc, const BCRec& phys_bc)
{
  const int* lo_bc = phys_bc.lo();
  const int* hi_bc = phys_bc.hi();
  bc.setLo(0,tang_vel_bc[lo_bc[0]]);
  bc.setHi(0,tang_vel_bc[hi_bc[0]]);
#if (BL_SPACEDIM >= 2)
  bc.setLo(1,norm_vel_bc[lo_bc[1]]);
  bc.setHi(1,norm_vel_bc[hi_bc[1]]);
#endif
#if (BL_SPACEDIM == 3)
  bc.setLo(2,tang_vel_bc[lo_bc[2]]);
  bc.setHi(2,tang_vel_bc[hi_bc[2]]);
#endif
}

static
void
set_z_vel_bc(BCRec& bc, const BCRec& phys_bc)
{
  const int* lo_bc = phys_bc.lo();
  const int* hi_bc = phys_bc.hi();
  bc.setLo(0,tang_vel_bc[lo_bc[0]]);
  bc.setHi(0,tang_vel_bc[hi_bc[0]]);
#if (BL_SPACEDIM >= 2)
  bc.setLo(1,tang_vel_bc[lo_bc[1]]);
  bc.setHi(1,tang_vel_bc[hi_bc[1]]);
#endif
#if (BL_SPACEDIM == 3)
  bc.setLo(2,norm_vel_bc[lo_bc[2]]);
  bc.setHi(2,norm_vel_bc[hi_bc[2]]);
#endif
}

void
Castro::variableSetUp ()
{

  // Castro::variableSetUp is called in the constructor of Amr.cpp, so
  // it should get called every time we start or restart a job


  // initialize the start time for our CPU-time tracker
  startCPUTime = ParallelDescriptor::second();


  // Output the git commit hashes used to build the executable.

  if (ParallelDescriptor::IOProcessor()) {

    const char* castro_hash  = buildInfoGetGitHash(1);
    const char* boxlib_hash  = buildInfoGetGitHash(2);
    const char* microphysics_hash = buildInfoGetGitHash(3);
    const char* buildgithash = buildInfoGetBuildGitHash();
    const char* buildgitname = buildInfoGetBuildGitName();

    if (strlen(castro_hash) > 0) {
      std::cout << "\n" << "Castro git describe: " << castro_hash << "\n";
    }
    if (strlen(boxlib_hash) > 0) {
      std::cout << "BoxLib git describe: " << boxlib_hash << "\n";
    }
    if (strlen(microphysics_hash) > 0) {
      std::cout << "Microphysics git describe: " << microphysics_hash << "\n";
    }
    if (strlen(buildgithash) > 0){
      std::cout << buildgitname << " git describe: " << buildgithash << "\n";
    }

    std::cout << "\n";
  }

  BL_ASSERT(desc_lst.size() == 0);

  // Get options, set phys_bc
  read_params();

  // Initialize the runtime parameters for any of the external
  // microphysics
  extern_init();

  // Initialize the network
  network_init();

#ifdef REACTIONS
  // Initialize the burner
  burner_init();
#endif

  //
  // Set number of state variables and pointers to components
  //

  int cnt = 0;
  Density = cnt++;
  Xmom = cnt++;
  Ymom = cnt++;
  Zmom = cnt++;
#ifdef HYBRID_MOMENTUM
  Rmom = cnt++;
  Lmom = cnt++;
  Pmom = cnt++;
#endif
  Eden = cnt++;
  Eint = cnt++;
  Temp = cnt++;

#ifdef NUM_ADV
  NumAdv = NUM_ADV;
#else
  NumAdv = 0;
#endif

  if (NumAdv > 0)
    {
      FirstAdv = cnt;
      cnt += NumAdv;
    }

  int dm = BL_SPACEDIM;

  // Get the number of species from the network model.
  ca_get_num_spec(&NumSpec);

  if (NumSpec > 0)
    {
      FirstSpec = cnt;
      cnt += NumSpec;
    }

  // Get the number of auxiliary quantities from the network model.
  ca_get_num_aux(&NumAux);

  if (NumAux > 0)
    {
      FirstAux = cnt;
      cnt += NumAux;
    }

#ifdef SHOCK_VAR
  Shock = cnt++;
#endif

  NUM_STATE = cnt;

  // Define NUM_GROW from the f90 module.
  ca_get_method_params(&NUM_GROW);

  const Real run_strt = ParallelDescriptor::second() ;


  // we want const_grav in F90, get it here from parmparse, since it
  // it not in the Castro namespace
  ParmParse pp("gravity");

  // Pass in the name of the gravity type we're using -- we do this
  // manually, since the Fortran parmparse doesn't support strings
  std::string gravity_type = "none";
  pp.query("gravity_type", gravity_type);
  int gravity_type_length = gravity_type.length();
  Array<int> gravity_type_name(gravity_type_length);

  for (int i = 0; i < gravity_type_length; i++)
    gravity_type_name[i] = gravity_type[i];


  // Read in the input values to Fortran.

  ca_set_castro_method_params();

  ca_set_method_params(dm, Density, Xmom, Eden, Eint, Temp, FirstAdv, FirstSpec, FirstAux,
		       NumAdv,
#ifdef SHOCK_VAR
		       Shock,
#endif
		       gravity_type_name.dataPtr(), gravity_type_length);

  // Get the number of primitive variables from Fortran.

  ca_get_qvar(&QVAR);
  ca_get_nqaux(&NQAUX);

  Real run_stop = ParallelDescriptor::second() - run_strt;

  ParallelDescriptor::ReduceRealMax(run_stop,ParallelDescriptor::IOProcessorNumber());

  if (ParallelDescriptor::IOProcessor())
    std::cout << "\nTime in ca_set_method_params: " << run_stop << '\n' ;

  int coord_type = Geometry::Coord();

  // Get the center variable from the inputs and pass it directly to Fortran.
  Array<Real> center(BL_SPACEDIM, 0.0);
  ParmParse ppc("castro");
  ppc.queryarr("center",center,0,BL_SPACEDIM);

  ca_set_problem_params(dm,phys_bc.lo(),phys_bc.hi(),
			Interior,Inflow,Outflow,Symmetry,SlipWall,NoSlipWall,coord_type,
			Geometry::ProbLo(),Geometry::ProbHi(),center.dataPtr());

  // Read in the parameters for the tagging criteria
  // and store them in the Fortran module.

  int probin_file_length = probin_file.length();
  Array<int> probin_file_name(probin_file_length);

  for (int i = 0; i < probin_file_length; i++)
    probin_file_name[i] = probin_file[i];

  ca_get_tagging_params(probin_file_name.dataPtr(),&probin_file_length);

#ifdef SPONGE
  // Read in the parameters for the sponge
  // and store them in the Fortran module.

  ca_get_sponge_params(probin_file_name.dataPtr(),&probin_file_length);
#endif

  Interpolater* interp;

  if (state_interp_order == 0) {
    interp = &pc_interp;
  }
  else {
    if (lin_limit_state_interp == 1)
      interp = &lincc_interp;
    else
      interp = &cell_cons_interp;
  }

#ifdef RADIATION
  // cell_cons_interp is not conservative in spherical coordinates.
  // We could do this for other cases too, but I'll confine it to
  // neutrino problems for now so as not to change the results of
  // other people's tests.  Better to fix cell_cons_interp!

  if (Geometry::IsSPHERICAL() && Radiation::nNeutrinoSpecies > 0) {
    interp = &pc_interp;
  }
#endif

  // Note that the default is state_data_extrap = false,
  // store_in_checkpoint = true.  We only need to put these in
  // explicitly if we want to do something different,
  // like not store the state data in a checkpoint directory
  bool state_data_extrap = false;
  bool store_in_checkpoint;

#ifdef RADIATION
  // Radiation should always have at least one ghost zone.
  int ngrow_state = std::max(1, state_nghost);
#else
  int ngrow_state = state_nghost;
#endif

  BL_ASSERT(ngrow_state >= 0);

  store_in_checkpoint = true;
  desc_lst.addDescriptor(State_Type,IndexType::TheCellType(),
			 StateDescriptor::Point,ngrow_state,NUM_STATE,
			 interp,state_data_extrap,store_in_checkpoint);

#ifdef SELF_GRAVITY
  store_in_checkpoint = true;
  desc_lst.addDescriptor(PhiGrav_Type, IndexType::TheCellType(),
			 StateDescriptor::Point, 1, 1,
			 &cell_cons_interp, state_data_extrap,
			 store_in_checkpoint);

  store_in_checkpoint = false;
  desc_lst.addDescriptor(Gravity_Type,IndexType::TheCellType(),
			 StateDescriptor::Point,NUM_GROW,3,
			 &cell_cons_interp,state_data_extrap,store_in_checkpoint);
#endif

  // Source terms. Currently this holds dS/dt for each of the NVAR state variables.

  store_in_checkpoint = true;
  desc_lst.addDescriptor(Source_Type, IndexType::TheCellType(),
			 StateDescriptor::Point,NUM_GROW,NUM_STATE,
			 &cell_cons_interp, state_data_extrap,store_in_checkpoint);

#ifdef ROTATION
  store_in_checkpoint = false;
  desc_lst.addDescriptor(PhiRot_Type, IndexType::TheCellType(),
			 StateDescriptor::Point, 1, 1,
			 &cell_cons_interp, state_data_extrap,
			 store_in_checkpoint);

  store_in_checkpoint = false;
  desc_lst.addDescriptor(Rotation_Type,IndexType::TheCellType(),
			 StateDescriptor::Point,NUM_GROW,3,
			 &cell_cons_interp,state_data_extrap,store_in_checkpoint);
#endif


#ifdef REACTIONS
  // Components 0:Numspec-1         are      omegadot_i
  // Component    NumSpec            is      enuc =      (eout-ein)
  // Component    NumSpec+1          is  rho_enuc= rho * (eout-ein)
  store_in_checkpoint = true;
  desc_lst.addDescriptor(Reactions_Type,IndexType::TheCellType(),
			 StateDescriptor::Point,0,NumSpec+2,
			 &cell_cons_interp,state_data_extrap,store_in_checkpoint);
#endif

#ifdef SDC
  // For SDC we want to store the source terms.

  store_in_checkpoint = true;
  desc_lst.addDescriptor(SDC_Source_Type, IndexType::TheCellType(),
			 StateDescriptor::Point,NUM_GROW,NUM_STATE,
			 &cell_cons_interp,state_data_extrap,store_in_checkpoint);

  // We also want to store the reactions source.

#ifdef REACTIONS
  store_in_checkpoint = true;
  desc_lst.addDescriptor(SDC_React_Type, IndexType::TheCellType(),
			 StateDescriptor::Point,NUM_GROW,QVAR,
			 &cell_cons_interp,state_data_extrap,store_in_checkpoint);
#endif
#endif

  Array<BCRec>       bcs(NUM_STATE);
  Array<std::string> name(NUM_STATE);

  BCRec bc;
  cnt = 0;
  set_scalar_bc(bc,phys_bc); bcs[cnt] = bc; name[cnt] = "density";
  cnt++; set_x_vel_bc(bc,phys_bc);  bcs[cnt] = bc; name[cnt] = "xmom";
  cnt++; set_y_vel_bc(bc,phys_bc);  bcs[cnt] = bc; name[cnt] = "ymom";
  cnt++; set_z_vel_bc(bc,phys_bc);  bcs[cnt] = bc; name[cnt] = "zmom";
#ifdef HYBRID_MOMENTUM
  cnt++; set_scalar_bc(bc,phys_bc);  bcs[cnt] = bc; name[cnt] = "rmom";
  cnt++; set_scalar_bc(bc,phys_bc);  bcs[cnt] = bc; name[cnt] = "lmom";
  cnt++; set_scalar_bc(bc,phys_bc);  bcs[cnt] = bc; name[cnt] = "pmom";
#endif
  cnt++; set_scalar_bc(bc,phys_bc); bcs[cnt] = bc; name[cnt] = "rho_E";
  cnt++; set_scalar_bc(bc,phys_bc); bcs[cnt] = bc; name[cnt] = "rho_e";
  cnt++; set_scalar_bc(bc,phys_bc); bcs[cnt] = bc; name[cnt] = "Temp";

  for (int i=0; i<NumAdv; ++i)
    {
      char buf[64];
      sprintf(buf, "adv_%d", i);
      cnt++; set_scalar_bc(bc,phys_bc); bcs[cnt] = bc; name[cnt] = string(buf);
    }

  // Get the species names from the network model.
  std::vector<std::string> spec_names;
  for (int i = 0; i < NumSpec; i++) {
    int len = 20;
    Array<int> int_spec_names(len);
    // This call return the actual length of each string in "len"
    ca_get_spec_names(int_spec_names.dataPtr(),&i,&len);
    char char_spec_names[len+1];
    for (int j = 0; j < len; j++)
      char_spec_names[j] = int_spec_names[j];
    char_spec_names[len] = '\0';
    spec_names.push_back(std::string(char_spec_names));
  }

  if ( ParallelDescriptor::IOProcessor())
    {
      std::cout << NumSpec << " Species: " << std::endl;
      for (int i = 0; i < NumSpec; i++)
	std::cout << spec_names[i] << ' ' << ' ';
      std::cout << std::endl;
    }

  for (int i=0; i<NumSpec; ++i)
    {
      cnt++;
      set_scalar_bc(bc,phys_bc);
      bcs[cnt] = bc;
      name[cnt] = "rho_" + spec_names[i];
    }

  // Get the auxiliary names from the network model.
  std::vector<std::string> aux_names;
  for (int i = 0; i < NumAux; i++) {
    int len = 20;
    Array<int> int_aux_names(len);
    // This call return the actual length of each string in "len"
    ca_get_aux_names(int_aux_names.dataPtr(),&i,&len);
    char char_aux_names[len+1];
    for (int j = 0; j < len; j++)
      char_aux_names[j] = int_aux_names[j];
    char_aux_names[len] = '\0';
    aux_names.push_back(std::string(char_aux_names));
  }

  if ( ParallelDescriptor::IOProcessor())
    {
      std::cout << NumAux << " Auxiliary Variables: " << std::endl;
      for (int i = 0; i < NumAux; i++)
	std::cout << aux_names[i] << ' ' << ' ';
      std::cout << std::endl;
    }

  for (int i=0; i<NumAux; ++i)
    {
      cnt++;
      set_scalar_bc(bc,phys_bc);
      bcs[cnt] = bc;
      name[cnt] = "rho_" + aux_names[i];
    }

#ifdef SHOCK_VAR
  cnt++; set_scalar_bc(bc,phys_bc); bcs[cnt] = bc; name[cnt] = "Shock";
#endif

  desc_lst.setComponent(State_Type,
			Density,
			name,
			bcs,
			BndryFunc(ca_denfill,ca_hypfill));

#ifdef SELF_GRAVITY
  set_scalar_bc(bc,phys_bc);
  desc_lst.setComponent(PhiGrav_Type,0,"phiGrav",bc,BndryFunc(ca_phigravfill));
  set_x_vel_bc(bc,phys_bc);
  desc_lst.setComponent(Gravity_Type,0,"grav_x",bc,BndryFunc(ca_gravxfill));
  set_y_vel_bc(bc,phys_bc);
  desc_lst.setComponent(Gravity_Type,1,"grav_y",bc,BndryFunc(ca_gravyfill));
  set_z_vel_bc(bc,phys_bc);
  desc_lst.setComponent(Gravity_Type,2,"grav_z",bc,BndryFunc(ca_gravzfill));
#endif

#ifdef ROTATION
  set_scalar_bc(bc,phys_bc);
  desc_lst.setComponent(PhiRot_Type,0,"phiRot",bc,BndryFunc(ca_phirotfill));
  set_x_vel_bc(bc,phys_bc);
  desc_lst.setComponent(Rotation_Type,0,"rot_x",bc,BndryFunc(ca_rotxfill));
  set_y_vel_bc(bc,phys_bc);
  desc_lst.setComponent(Rotation_Type,1,"rot_y",bc,BndryFunc(ca_rotyfill));
  set_z_vel_bc(bc,phys_bc);
  desc_lst.setComponent(Rotation_Type,2,"rot_z",bc,BndryFunc(ca_rotzfill));
#endif

  // Source term array will use standard hyperbolic fill.

  Array<std::string> state_type_source_names(NUM_STATE);

  for (int i = 0; i < NUM_STATE; i++)
    state_type_source_names[i] = name[i] + "_source";

  desc_lst.setComponent(Source_Type,Density,state_type_source_names,bcs,BndryFunc(ca_denfill,ca_hypfill));

#ifdef REACTIONS
  std::string name_react;
  for (int i=0; i<NumSpec; ++i)
    {
      set_scalar_bc(bc,phys_bc);
      name_react = "omegadot_" + spec_names[i];
      desc_lst.setComponent(Reactions_Type, i, name_react, bc,BndryFunc(ca_reactfill));
    }
  desc_lst.setComponent(Reactions_Type, NumSpec  , "enuc", bc, BndryFunc(ca_reactfill));
  desc_lst.setComponent(Reactions_Type, NumSpec+1, "rho_enuc", bc, BndryFunc(ca_reactfill));
#endif

#ifdef SDC
  for (int i = 0; i < NUM_STATE; ++i)
      state_type_source_names[i] = "sdc_sources_" + name[i];
  desc_lst.setComponent(SDC_Source_Type,Density,state_type_source_names,bcs,BndryFunc(ca_denfill,ca_hypfill));
#ifdef REACTIONS
  for (int i = 0; i < QVAR; ++i) {
      char buf[64];
      sprintf(buf, "sdc_react_source_%d", i);
      set_scalar_bc(bc,phys_bc);
      desc_lst.setComponent(SDC_React_Type,i,std::string(buf),bc,BndryFunc(ca_denfill));
  }
#endif
#endif

#ifdef RADIATION
  int ngrow = 1;
  int ncomp = Radiation::nGroups;
  desc_lst.addDescriptor(Rad_Type, IndexType::TheCellType(),
			 StateDescriptor::Point, ngrow, ncomp,
			 interp);
  set_scalar_bc(bc,phys_bc);

  if (ParallelDescriptor::IOProcessor()) {
    std::cout << "Radiation::nGroups = " << Radiation::nGroups << std::endl;
    std::cout << "Radiation::nNeutrinoSpecies = "
	      << Radiation::nNeutrinoSpecies << std::endl;
    if (Radiation::nNeutrinoSpecies > 0) {
      std::cout << "Radiation::nNeutrinoGroups  = ";
      for (int n = 0; n < Radiation::nNeutrinoSpecies; n++) {
	std::cout << " " << Radiation::nNeutrinoGroups[n];
      }
      std::cout << std::endl;
      if (Radiation::nNeutrinoGroups[0] > 0 &&
	  NumAdv != 0) {
	amrex::Error("Neutrino solver assumes NumAdv == 0");
      }
      if (Radiation::nNeutrinoGroups[0] > 0 &&
	  (NumSpec != 1 || NumAux != 1)) {
	amrex::Error("Neutrino solver assumes NumSpec == NumAux == 1");
      }
    }
  }

  char rad_name[10];
  if (!Radiation::do_multigroup) {
    desc_lst
      .setComponent(Rad_Type, Rad, "rad", bc,
		    BndryFunc(ca_radfill));
  }
  else {
    if (Radiation::nNeutrinoSpecies == 0 ||
	Radiation::nNeutrinoGroups[0] == 0) {
      for (int i = 0; i < Radiation::nGroups; i++) {
	sprintf(rad_name, "rad%d", i);
	desc_lst
	  .setComponent(Rad_Type, i, rad_name, bc,
			BndryFunc(ca_radfill));
      }
    }
    else {
      int indx = 0;
      for (int j = 0; j < Radiation::nNeutrinoSpecies; j++) {
	for (int i = 0; i < Radiation::nNeutrinoGroups[j]; i++) {
	  sprintf(rad_name, "rads%dg%d", j, i);
	  desc_lst.setComponent(Rad_Type, indx, rad_name, bc, BndryFunc(ca_radfill));
	  indx++;
	}
      }
    }
  }
#endif

  if (use_custom_knapsack_weights) {
      Knapsack_Weight_Type = desc_lst.size();
      desc_lst.addDescriptor(Knapsack_Weight_Type, IndexType::TheCellType(), StateDescriptor::Point,
			     0, 1, &pc_interp);
      // Because we use piecewise constant interpolation, we do not use bc and BndryFunc.
      desc_lst.setComponent(Knapsack_Weight_Type, 0, "KnapsackWeight",
			    bc, BndryFunc(ca_nullfill));
  }

  num_state_type = desc_lst.size();

  //
  // DEFINE DERIVED QUANTITIES
  //
  // Pressure
  //
  derive_lst.add("pressure",IndexType::TheCellType(),1,ca_derpres,the_same_box);
  derive_lst.addComponent("pressure",desc_lst,State_Type,Density,NUM_STATE);

  //
  // Kinetic energy
  //
  derive_lst.add("kineng",IndexType::TheCellType(),1,ca_derkineng,the_same_box);
  derive_lst.addComponent("kineng",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("kineng",desc_lst,State_Type,Xmom,3);

  //
  // Sound speed (c)
  //
  derive_lst.add("soundspeed",IndexType::TheCellType(),1,ca_dersoundspeed,the_same_box);
  derive_lst.addComponent("soundspeed",desc_lst,State_Type,Density,NUM_STATE);

  //
  // Mach number(M)
  //
  derive_lst.add("MachNumber",IndexType::TheCellType(),1,ca_dermachnumber,the_same_box);
  derive_lst.addComponent("MachNumber",desc_lst,State_Type,Density,NUM_STATE);

#if (BL_SPACEDIM == 1)
  //
  // Wave speed u+c
  //
  derive_lst.add("uplusc",IndexType::TheCellType(),1,ca_deruplusc,the_same_box);
  derive_lst.addComponent("uplusc",desc_lst,State_Type,Density,NUM_STATE);

  //
  // Wave speed u-c
  //
  derive_lst.add("uminusc",IndexType::TheCellType(),1,ca_deruminusc,the_same_box);
  derive_lst.addComponent("uminusc",desc_lst,State_Type,Density,NUM_STATE);
#endif

  //
  // Gravitational forcing
  //
#ifdef SELF_GRAVITY
  //    derive_lst.add("rhog",IndexType::TheCellType(),1,
  //                   BL_FORT_PROC_CALL(CA_RHOG,ca_rhog),the_same_box);
  //    derive_lst.addComponent("rhog",desc_lst,State_Type,Density,1);
  //    derive_lst.addComponent("rhog",desc_lst,Gravity_Type,0,BL_SPACEDIM);
#endif

  //
  // Entropy (S)
  //
  derive_lst.add("entropy",IndexType::TheCellType(),1,ca_derentropy,the_same_box);
  derive_lst.addComponent("entropy",desc_lst,State_Type,Density,NUM_STATE);

#ifdef DIFFUSION
  if (diffuse_temp) {
    //
    // thermal conductivity (k_th)
    //
    derive_lst.add("thermal_cond",IndexType::TheCellType(),1,ca_dercond,the_same_box);
    derive_lst.addComponent("thermal_cond",desc_lst,State_Type,Density,NUM_STATE);


    //
    // thermal diffusivity (k_th/(rho c_v))
    //    
    derive_lst.add("diff_coeff",IndexType::TheCellType(),1,ca_derdiffcoeff,the_same_box);
    derive_lst.addComponent("diff_coeff",desc_lst,State_Type,Density,NUM_STATE);


    //
    // diffusion term (the divergence of thermal flux)
    //    
    derive_lst.add("diff_term",IndexType::TheCellType(),1,ca_derdiffterm,grow_box_by_one);
    derive_lst.addComponent("diff_term",desc_lst,State_Type,Density,NUM_STATE);
    

  }
#endif

  //
  // Vorticity
  //
  derive_lst.add("magvort",IndexType::TheCellType(),1,ca_dermagvort,grow_box_by_one);
  // Here we exploit the fact that Xmom = Density + 1
  //   in order to use the correct interpolation.
  if (Xmom != Density+1)
    amrex::Error("We are assuming Xmom = Density + 1 in Castro_setup.cpp");
  derive_lst.addComponent("magvort",desc_lst,State_Type,Density,4);

  //
  // Div(u)
  //
  derive_lst.add("divu",IndexType::TheCellType(),1,ca_derdivu,grow_box_by_one);
  derive_lst.addComponent("divu",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("divu",desc_lst,State_Type,Xmom,3);

  //
  // Internal energy as derived from rho*E, part of the state
  //
  derive_lst.add("eint_E",IndexType::TheCellType(),1,ca_dereint1,the_same_box);
  derive_lst.addComponent("eint_E",desc_lst,State_Type,Density,NUM_STATE);

  //
  // Internal energy as derived from rho*e, part of the state
  //
  derive_lst.add("eint_e",IndexType::TheCellType(),1,ca_dereint2,the_same_box);
  derive_lst.addComponent("eint_e",desc_lst,State_Type,Density,NUM_STATE);

  //
  // Log(density)
  //
  derive_lst.add("logden",IndexType::TheCellType(),1,ca_derlogden,the_same_box);
  derive_lst.addComponent("logden",desc_lst,State_Type,Density,NUM_STATE);

  derive_lst.add("StateErr",IndexType::TheCellType(),3,ca_derstate,grow_box_by_one);
  derive_lst.addComponent("StateErr",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("StateErr",desc_lst,State_Type,Temp,1);
  derive_lst.addComponent("StateErr",desc_lst,State_Type,FirstSpec,1);

  //
  // X from rhoX
  //
  for (int i = 0; i < NumSpec; i++){
    std::string spec_string = "X("+spec_names[i]+")";
    derive_lst.add(spec_string,IndexType::TheCellType(),1,ca_derspec,the_same_box);
    derive_lst.addComponent(spec_string,desc_lst,State_Type,Density,1);
    derive_lst.addComponent(spec_string,desc_lst,State_Type,FirstSpec+i,1);
  }
  //
  // Velocities
  //
  derive_lst.add("x_velocity",IndexType::TheCellType(),1,ca_dervel,the_same_box);
  derive_lst.addComponent("x_velocity",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("x_velocity",desc_lst,State_Type,Xmom,1);

  derive_lst.add("y_velocity",IndexType::TheCellType(),1,ca_dervel,the_same_box);
  derive_lst.addComponent("y_velocity",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("y_velocity",desc_lst,State_Type,Ymom,1);

  derive_lst.add("z_velocity",IndexType::TheCellType(),1,ca_dervel,the_same_box);
  derive_lst.addComponent("z_velocity",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("z_velocity",desc_lst,State_Type,Zmom,1);

#ifdef REACTIONS
  //
  // Nuclear energy generation timescale t_e == e / edot
  // Sound-crossing time t_s == dx / c_s
  // Ratio of these is t_s_t_e == t_s / t_e
  //
  derive_lst.add("t_sound_t_enuc",IndexType::TheCellType(),1,ca_derenuctimescale,the_same_box);
  derive_lst.addComponent("t_sound_t_enuc",desc_lst,State_Type,Density,NUM_STATE);
  derive_lst.addComponent("t_sound_t_enuc",desc_lst,Reactions_Type,NumSpec,1);
#endif

  derive_lst.add("magvel",IndexType::TheCellType(),1,ca_dermagvel,the_same_box);
  derive_lst.addComponent("magvel",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("magvel",desc_lst,State_Type,Xmom,3);

  derive_lst.add("radvel",IndexType::TheCellType(),1,ca_derradialvel,the_same_box);
  derive_lst.addComponent("radvel",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("radvel",desc_lst,State_Type,Xmom,3);

  derive_lst.add("magmom",IndexType::TheCellType(),1,ca_dermagmom,the_same_box);
  derive_lst.addComponent("magmom",desc_lst,State_Type,Xmom,3);

  derive_lst.add("angular_momentum_x",IndexType::TheCellType(),1,ca_derangmomx,the_same_box);
  derive_lst.addComponent("angular_momentum_x",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("angular_momentum_x",desc_lst,State_Type,Xmom,3);

  derive_lst.add("angular_momentum_y",IndexType::TheCellType(),1,ca_derangmomy,the_same_box);
  derive_lst.addComponent("angular_momentum_y",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("angular_momentum_y",desc_lst,State_Type,Xmom,3);

  derive_lst.add("angular_momentum_z",IndexType::TheCellType(),1,ca_derangmomz,the_same_box);
  derive_lst.addComponent("angular_momentum_z",desc_lst,State_Type,Density,1);
  derive_lst.addComponent("angular_momentum_z",desc_lst,State_Type,Xmom,3);

#ifdef SELF_GRAVITY
  derive_lst.add("maggrav",IndexType::TheCellType(),1,ca_dermaggrav,the_same_box);
  derive_lst.addComponent("maggrav",desc_lst,Gravity_Type,0,3);
#endif

#ifdef PARTICLES
  //
  // We want a derived type that corresponds to the number of particles
  // in each cell.  We only intend to use it in plotfiles for debugging
  // purposes.  We'll just use the DERNULL since don't do anything in
  // fortran for now.  We'll actually set the values in writePlotFile().
  //
  derive_lst.add("particle_count",IndexType::TheCellType(),1,ca_dernull,the_same_box);
  derive_lst.addComponent("particle_count",desc_lst,State_Type,Density,1);

  derive_lst.add("total_particle_count",IndexType::TheCellType(),1,ca_dernull,the_same_box);
  derive_lst.addComponent("total_particle_count",desc_lst,State_Type,Density,1);
#endif

#ifdef RADIATION
  if (Radiation::do_multigroup) {
    derive_lst.add("Ertot", IndexType::TheCellType(),1,ca_derertot,the_same_box);
    derive_lst.addComponent("Ertot",desc_lst,Rad_Type,0,Radiation::nGroups);
  }
#endif

#ifdef NEUTRINO
  if (Radiation::nNeutrinoSpecies > 0 &&
      Radiation::plot_neutrino_group_energies_per_MeV) {
    char rad_name[10];
    int indx = 0;
    for (int j = 0; j < Radiation::nNeutrinoSpecies; j++) {
      for (int i = 0; i < Radiation::nNeutrinoGroups[j]; i++) {
	sprintf(rad_name, "Neuts%dg%d", j, i);
	derive_lst.add(rad_name,IndexType::TheCellType(),1,ca_derneut,the_same_box);
	derive_lst.addComponent(rad_name,desc_lst,Rad_Type,indx,1);
	indx++;
      }
    }
  }

  if (Radiation::nNeutrinoSpecies > 0 &&
      Radiation::nNeutrinoGroups[0] > 0) {
    derive_lst.add("Enue", IndexType::TheCellType(),1,ca_derenue,the_same_box);
    derive_lst.addComponent("Enue",desc_lst,Rad_Type,0,Radiation::nGroups);
    derive_lst.add("Enuae", IndexType::TheCellType(),1,ca_derenuae,the_same_box);
    derive_lst.addComponent("Enuae",desc_lst,Rad_Type,0,Radiation::nGroups);
    //
    // rho_Yl = rho(Ye + Ynue - Ynuebar)
    //
    derive_lst.add("rho_Yl",IndexType::TheCellType(),1,ca_derrhoyl,the_same_box);
    // Don't actually need density for rho * Yl
    derive_lst.addComponent("rho_Yl",desc_lst,State_Type,Density,1);
    // FirstAux is (rho * Ye)
    derive_lst.addComponent("rho_Yl",desc_lst,State_Type,FirstAux,1);
    derive_lst.addComponent("rho_Yl",desc_lst,Rad_Type,0,Radiation::nGroups);
    //
    // Yl = (Ye + Ynue - Ynuebar)
    //
    derive_lst.add("Yl",IndexType::TheCellType(),1,ca_deryl,the_same_box);
    derive_lst.addComponent("Yl",desc_lst,State_Type,Density,1);
    // FirstAux is (rho * Ye)
    derive_lst.addComponent("Yl",desc_lst,State_Type,FirstAux,1);
    derive_lst.addComponent("Yl",desc_lst,Rad_Type,0,Radiation::nGroups);
    //
    // Ynue
    //
    derive_lst.add("Ynue",IndexType::TheCellType(),1,ca_derynue,the_same_box);
    derive_lst.addComponent("Ynue",desc_lst,State_Type,Density,1);
    // FirstAux is (rho * Ye)
    derive_lst.addComponent("Ynue",desc_lst,State_Type,FirstAux,1);
    derive_lst.addComponent("Ynue",desc_lst,Rad_Type,0,Radiation::nGroups);
    //
    // Ynuebar
    //
    derive_lst.add("Ynuae",IndexType::TheCellType(),1,ca_derynuae,the_same_box);
    derive_lst.addComponent("Ynuae",desc_lst,State_Type,Density,1);
    // FirstAux is (rho * Ye)
    derive_lst.addComponent("Ynuae",desc_lst,State_Type,FirstAux,1);
    derive_lst.addComponent("Ynuae",desc_lst,Rad_Type,0,Radiation::nGroups);
  }
#endif


  for (int i = 0; i < NumAux; i++)  {
    derive_lst.add(aux_names[i],IndexType::TheCellType(),1,ca_derspec,the_same_box);
    derive_lst.addComponent(aux_names[i],desc_lst,State_Type,Density,1);
    derive_lst.addComponent(aux_names[i],desc_lst,State_Type,FirstAux+i,1);
  }

#if 0
  //
  // A derived quantity equal to all the state variables.
  //
  derive_lst.add("FULLSTATE",IndexType::TheCellType(),NUM_STATE,FORT_DERCOPY,the_same_box);
  derive_lst.addComponent("FULLSTATE",desc_lst,State_Type,Density,NUM_STATE);

#endif


  //
  // Problem-specific adds
#include <Problem_Derives.H>

  //
  // DEFINE ERROR ESTIMATION QUANTITIES
  //
  ErrorSetUp();

  //
  // Construct an array holding the names of the source terms.
  //

  source_names.resize(num_src);

  // Fill with an empty string to initialize.

  for (int n = 0; n < num_src; ++n)
    source_names[n] = "";

  source_names[ext_src] = "user-defined external";

#ifdef SPONGE
  source_names[sponge_src] = "sponge";
#endif

#ifdef DIFFUSION
  source_names[diff_src] = "diffusion";
#endif

#ifdef HYBRID_MOMENTUM
  source_names[hybrid_src] = "hybrid";
#endif

#ifdef GRAVITY
  source_names[grav_src] = "gravity";
#endif

#ifdef ROTATION
  source_names[rot_src] = "rotation";
#endif


  // method of lines Butcher tableau
#define THIRDORDER_TVD

#ifdef THIRDORDER
  MOL_STAGES = 3;

  a_mol.resize(MOL_STAGES);
  for (int n = 0; n < MOL_STAGES; ++n)
    a_mol[n].resize(MOL_STAGES);

  a_mol[0] = {0,   0, 0};
  a_mol[1] = {0.5, 0, 0};
  a_mol[2] = {-1,  2, 0};

  b_mol = {1./6., 2./3., 1./6.};

  c_mol = {0.0, 0.5, 1};
#endif

#ifdef THIRDORDER_TVD
  MOL_STAGES = 3;

  a_mol.resize(MOL_STAGES);
  for (int n = 0; n < MOL_STAGES; ++n)
    a_mol[n].resize(MOL_STAGES);

  a_mol[0] = {0.0,  0.0,  0.0};
  a_mol[1] = {1.0,  0.0,  0.0};
  a_mol[2] = {0.25, 0.25, 0.0};

  b_mol = {1./6., 1./6., 2./3.};

  c_mol = {0.0, 1.0, 0.5};
#endif

#ifdef SECONDORDER
  MOL_STAGES = 2;

  a_mol.resize(MOL_STAGES);
  for (int n = 0; n < MOL_STAGES; ++n)
    a_mol[n].resize(MOL_STAGES);

  a_mol[0] = {0,   0,};
  a_mol[1] = {0.5, 0,};

  b_mol = {0.0, 1.0};

  c_mol = {0.0, 0.5};
#endif

#ifdef SECONDORDER_TVD
  MOL_STAGES = 2;

  a_mol.resize(MOL_STAGES);
  for (int n = 0; n < MOL_STAGES; ++n)
    a_mol[n].resize(MOL_STAGES);

  a_mol[0] = {0,   0,};
  a_mol[1] = {1.0, 0,};

  b_mol = {0.5, 0.5};

  c_mol = {0.0, 1.0};
#endif

#ifdef FIRSTORDER
  MOL_STAGES = 1;

  a_mol.resize(MOL_STAGES);
  for (int n = 0; n < MOL_STAGES; ++n)
    a_mol[n].resize(MOL_STAGES);

  a_mol[0] = {1};
  b_mol = {1.0};
  c_mol = {0.0};
#endif

}
