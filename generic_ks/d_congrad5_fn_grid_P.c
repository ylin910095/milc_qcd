/******************* d_congrad5_fn_grid.c ************************/
/* For the Grid interface */
/* MIMD version 7 */

/* This is the MILC wrapper for the Grid single-mass inverter */

/* NOTE: This code is actually an include file for d_congrad5_fn_grid_F.c
   and d_congrad5_fn_grid_D.c, so any edits should be consistent with this
   purpose. */

/* Entry points (must be redefined to precision-specific names)

   KS_CONGRAD_PARITY_GRID

*/

/* Redefinitions according to requested precision */

#if ( GRID_PrecisionInt == 1 )

#define KS_CONGRAD_PARITY_GRID   ks_congrad_parity_grid_F
#define MYREAL GRID_F_Real

#else

#define KS_CONGRAD_PARITY_GRID   ks_congrad_parity_grid_D
#define MYREAL GRID_D_Real

#endif

#include "generic_ks_includes.h"
#include "../include/generic_grid.h"
#include "../include/generic_ks_grid.h"
#include "../include/openmp_defs.h"
#include <assert.h>
#ifdef VTUNE
#include <ittnotify.h>
#endif

/* Load inversion args for Level 3 inverter */

static void 
set_grid_invert_arg( GRID_invert_arg_t* grid_invert_arg, 
		     quark_invert_control *qic )
{
  grid_invert_arg->parity       = milc2grid_parity(qic->parity);
  grid_invert_arg->max          = qic->max;
  grid_invert_arg->nrestart     = qic->nrestart;
}


static GRID_resid_arg_t *
create_grid_resid_arg( quark_invert_control *qic )
{
  GRID_resid_arg_t *res_arg;
  char myname[] = "create_grid_resid_arg";
  int isrc,imass;

  /* Pointers for residual errors */
  res_arg = (GRID_resid_arg_t *)malloc(sizeof(GRID_resid_arg_t ));
  if(res_arg == NULL){
    printf("%s(%d): Can't allocate res_arg\n",myname,this_node);
    terminate(1);
  }
  *res_arg = GRID_RESID_ARG_DEFAULT;
  /* For now the residuals are the same for all sources and masses */
  res_arg->resid        = qic->resid;
  res_arg->relresid     = 0.;  /* NOT SUPPORTED */
  res_arg->final_rsq    = 0.;
  res_arg->final_rel    = 0.;

  return res_arg;
}

/* Collect inversion statistics */

static void 
get_grid_resid_arg( quark_invert_control *qic, 
		    GRID_resid_arg_t* grid_resid_arg )
{
  qic->final_rsq     = grid_resid_arg->final_rsq;
  qic->final_relrsq  = 0.;                          /* Not supported at the moment */
  qic->size_r        = grid_resid_arg->size_r;
  qic->size_relr     = grid_resid_arg->size_relr;
  qic->final_iters   = grid_resid_arg->final_iter;
  qic->final_restart = grid_resid_arg->final_restart;
}

static void
destroy_grid_resid_arg(GRID_resid_arg_t *res_arg)
{
  free(res_arg);
}


/*! \brief call to the grid_ks_congrad_parity.
 *
 * Expects that setup_mbench has been called already, so that we do not have to 
 * malloc things agian.
 */
int
KS_CONGRAD_PARITY_GRID ( su3_vector *src,
			 su3_vector *sol,
			 quark_invert_control *qic,
			 Real mass,
			 fn_links_t *fn)			 
{
  char myname[] = "ks_congrad_parity_grid";
  double nflop = 1187;
  int iters = 0;
  GRID_info_t info = GRID_INFO_ZERO;
  GRID_ColorVector *grid_sol, *grid_src;
  GRID_resid_arg_t *grid_resid_arg;
  GRID_invert_arg_t grid_invert_arg = GRID_INVERT_ARG_DEFAULT;
  GRID_FermionLinksAsqtad  *links;    
  
  double tot_cg_time = -dclock();
  
  if(! grid_initialized()){
    node0_printf("%s: FATAL Grid has not been initialized\n", myname);
    terminate(1);
  }

  /* Set grid_invert_arg */
  set_grid_invert_arg( & grid_invert_arg, qic );
  
  /* Pointers for residual errors */
  grid_resid_arg = create_grid_resid_arg( qic );
  
  /* Data layout conversions */
  
  double t_sp1, t_sp2, t_l;
  t_sp1 = -dclock();
  
  node0_printf("Calling GRID_create_V_from_vec\n"); fflush(stdout);
  grid_src = GRID_create_V_from_vec( src, qic->parity);
  
  t_sp1 += dclock();
  t_sp2 = -dclock();
  
  node0_printf("Calling GRID_create_V_from_vec\n"); fflush(stdout);
  grid_sol = GRID_create_V_from_vec( sol, qic->parity);
  
  t_sp2 += dclock();
  t_l   = -dclock(); 
  
  node0_printf("Calling GRID_asqtad_create_L_from_MILC\n"); fflush(stdout);
  /* For now we are taking the thin links from the site structure, so the first parameter is NULL */
  links = GRID_asqtad_create_L_from_MILC( NULL, get_fatlinks(fn), get_lnglinks(fn), EVENANDODD );

  t_l   += dclock(); 
  
  
  double dtimegridinv = -dclock();
  
  node0_printf("Calling GRID_asqtad_invert\n"); fflush(stdout);
  GRID_asqtad_invert( &info, links, &grid_invert_arg, 
		      grid_resid_arg, (MYREAL)mass, grid_sol, grid_src );
  iters = grid_resid_arg->final_iter;
  
  dtimegridinv += dclock();
  double dtimeinv = info.final_sec;
  double t_gr = dtimegridinv - dtimeinv - info.misc_sec;
  
  get_grid_resid_arg(qic, grid_resid_arg);
  
  /* Free the structure */
  destroy_grid_resid_arg(grid_resid_arg);
  
  /* Copy results back to su3_vector */
  double t_sl = -dclock();
  GRID_extract_V_to_vec( sol, grid_sol, qic->parity);
  t_sl += dclock();

#if 0
  /* Fix normalization */
  int i;
  Real rescale = 1./(2.*mass);
  FORSOMEFIELDPARITY_OMP(i, qic->parity, default(shared)){
    scalar_mult_su3_vector( sol+i, rescale, sol+i);
  } END_LOOP_OMP;
#endif
  
  /* Free GRID fields  */
  
  GRID_destroy_V(grid_src);    
  GRID_destroy_V(grid_sol);     
  GRID_asqtad_destroy_L(links);
  
  tot_cg_time +=dclock();

#ifdef CGTIME
  char *prec_label[2] = {"F", "D"};
  if(this_node==0) {
    printf("CONGRAD5: time = %e "
	   "(Grid %s) masses = 1 iters = %d "
	   "mflops = %e "
	   "\n"
	   , tot_cg_time
	   , prec_label[MILC_PRECISION-1], iters
	   , (double)(nflop*volume*iters/(1.0e6*tot_cg_time*numnodes()))
	   );
    fflush(stdout);
  }
#ifdef REMAP
  if(this_node==0) {
    printf("MILC<-->Grid data layout conversion timings\n"
	   "\t src-spinor   = %e\n"
	   "\t dest-spinors = %e\n"
	   "\t soln-spinor  = %e\n"
	   "\t links        = %e\n"
	   "\t Grid remap   = %e\n"
	   "\t ---------------------------\n"
	   "\t total remap  = %e\n"
	   , t_sp1, t_sp2, t_l, t_sl, t_gr
	   , t_sp1 + t_sp2 + t_l + t_sl + t_gr
	   );
    printf("CONGRAD5-Grid: "
	   "solve-time = %e "
	   "(Grid %s) masses = 1 iters = %d "
	   "mflops(ignore data-conv.) = %e "
	   "\n"
	   , dtimeinv
	   , prec_label[MILC_PRECISION-1], iters
	   , (double)(nflop*volume*iters/(1.0e6*dtimeinv*numnodes()))
	   );
    fflush(stdout);
  }
#endif
#endif

  return iters;
}


