/****************** fermion_links_asqtad_qop.c ***********************/
/* MIMD version 7 */

/* This is the MILC wrapper for SciDAC Level 3 QOP link smearing */

/* Note: This is an include file for fermion_links_asqtad_qop_F.c and
   fermion_links_asqtad_qop_D.c.  Any edits must be consistent with
   this use. */

/* Entry points (must be defined for the correct precision) */

/* 

   LOAD_FN_LINKS
   LOAD_FN_LINKS_DMDU0
   CREATE_QOP_ASQTAD_FERMION_LINKS
   DESTROY_QOP_ASQTAD_FERMION_LINKS
   INVALIDATE_FN_LINKS

*/

/* Redefinitions according to selected preicsion */

#if ( QOP_Precision == 1 )

#define MYREAL float
#define MYSU3_MATRIX fsu3_matrix
#define LOAD_QOP_ASQTAD_COEFFS load_qop_F_asqtad_coeffs
#define LOAD_FN_LINKS load_fn_links_F
#define CREATE_QOP_ASQTAD_FERMION_LINKS create_qop_F_asqtad_fermion_links
#define DESTROY_QOP_ASQTAD_FERMION_LINKS destroy_qop_F_asqtad_fermion_links
#define INVALIDATE_FN_LINKS invalidate_fn_links_F

#else

#define MYREAL double
#define MYSU3_MATRIX dsu3_matrix
#define LOAD_QOP_ASQTAD_COEFFS load_qop_D_asqtad_coeffs
#define LOAD_FN_LINKS load_fn_links_D
#define CREATE_QOP_ASQTAD_FERMION_LINKS create_qop_D_asqtad_fermion_links
#define DESTROY_QOP_ASQTAD_FERMION_LINKS destroy_qop_D_asqtad_fermion_links
#define INVALIDATE_FN_LINKS invalidate_fn_links_D

#endif

#include "generic_ks_includes.h"	/* definitions files and prototypes */
#include "../include/generic_qop.h"
#include "../include/generic_ks_qop.h"

#ifdef QCDOC
#define special_alloc qcdoc_alloc
#define special_free qfree
#else
#define special_alloc malloc
#define special_free free
#endif

static QOP_FermionLinksAsqtad *qop_links = NULL;
static int valid_fn_links = 0;
static int valid_fn_links_dmdu0 = 0;

/*********************************************************************/
/* Create QOP_FermionLinksAsqtad object from QOP gauge field */
/*********************************************************************/
static void
create_qop_asqtad_L_from_G(Real *act_path_coeff,
			   QOP_GaugeField *links)
{
  QOP_info_t info;
  QOP_asqtad_coeffs_t coeffs;
#ifdef LLTIME
  double nflopfl = 61632;
  double nflopll = 1804;
  double nflop = nflopfl + nflopll;
#endif
  double dtime = -dclock();

  LOAD_QOP_ASQTAD_COEFFS(&coeffs, 0.5, act_path_coeff);

  if(qop_links != NULL)
    QOP_asqtad_destroy_L(qop_links);

  qop_links = QOP_asqtad_create_L_from_G(&info, &coeffs, links);

  dtime += dclock();
#ifdef LLTIME
  node0_printf("LLTIME(total): time = %e (Asqtad opt) mflops = %e\n",dtime,
         (Real)nflop*volume/(1e6*dtime*numnodes()) );
#endif
}


static void 
load_asqtad_links(int both, su3_matrix **t_fl, su3_matrix **t_ll,
		  Real *act_path_coeff) {

  QOP_GaugeField *links;
  MYSU3_MATRIX **fatlinks;
  MYSU3_MATRIX **longlinks;
  MYSU3_MATRIX **raw_gauge_links;
  double remaptime = -dclock();
  char myname[] = "load_asqtad_links";

  if( phases_in != 1){
    node0_printf("load_fermion_links_fn: BOTCH: needs phases in\n");
    terminate(1);
  }

  /* Initialize QOP */
  if(initialize_qop() != QOP_SUCCESS){
    printf("%s(%d): Error initializing QOP\n",myname,this_node);
    terminate(1);
  }

  fatlinks = create_raw4_G();
  if(fatlinks == NULL)terminate(1);

  longlinks = create_raw4_G();
  if(longlinks == NULL)terminate(1);

  raw_gauge_links = create_raw4_G_from_site(F_OFFSET(link),EVENANDODD);
  links = QOP_create_G_from_raw((MYREAL **)(raw_gauge_links),QOP_EVENODD);
  destroy_raw4_G(raw_gauge_links);   raw_gauge_links = NULL;

  remaptime += dclock();
  create_qop_asqtad_L_from_G(act_path_coeff, links);
  remaptime -= dclock();
  QOP_asqtad_extract_L_to_raw((MYREAL **)fatlinks, (MYREAL **)longlinks, 
			      qop_links, QOP_EVENODD);

  /* Allocate space for t_fl if NULL */
  if(*t_fl == NULL){
    *t_fl = (su3_matrix *)special_alloc(sites_on_node*4*sizeof(su3_matrix));
    if(*t_fl==NULL){
      printf("%s(%d): no room for t_fl\n",myname,this_node);
      terminate(1);
    }
  }
  
  /* Allocate space for t_ll if NULL and we are doing both fat and long */
  if(*t_ll == NULL && both){
    *t_ll = (su3_matrix *)special_alloc(sites_on_node*4*sizeof(su3_matrix));
    if(*t_ll==NULL){
      printf("%s(%d): no room for t_ll\n",myname,this_node);
      terminate(1);
    }
  }
  
  unload_raw4_G_to_field(*t_fl,  fatlinks,  EVENANDODD);
  if(both)unload_raw4_G_to_field(*t_ll, longlinks, EVENANDODD);
  destroy_raw4_G(fatlinks);
  destroy_raw4_G(longlinks);
  QOP_destroy_G(links);

  remaptime += dclock();
#ifdef LLTIME
  node0_printf("LLREMAP:  time = %e\n",remaptime);
#endif
}

/*********************************************************************/
/* Create fat and long links and qop_links                           */
/*********************************************************************/
/* Wrappers for MILC call to QOP */
void 
LOAD_FN_LINKS( void ){
  if(valid_fn_links == 1)return;
  load_asqtad_links(1, &t_fatlink, &t_longlink, get_quark_path_coeff());

#ifdef DBLSTORE_FN
  load_fatbacklinks(&t_fatbacklink, t_fatlink);
  load_longbacklinks(&t_longbacklink, t_longlink);
#endif
  valid_fn_links = 1;
}

#ifdef DM_DU0
/* Wrappers for MILC call to QOP */
void LOAD_FN_LINKS_DMDU0( void ){
  su3_matrix *null = NULL;

  if(valid_fn_links_dmdu0 == 1)return;
  load_asqtad_links(0, &t_dfatlink_du0, &null, get_quark_path_coeff_dmdu0());
  valid_fn_links_dmdu0 = 1;
}
#endif

/*********************************************************************/
/* Create QOP_FermionLinksAsqtad object from MILC fat and long links */
/*********************************************************************/
QOP_FermionLinksAsqtad *
CREATE_QOP_ASQTAD_FERMION_LINKS( void )
{
  /* Create fat and long links if necessary */
  LOAD_FN_LINKS();
  return qop_links;
}

void
DESTROY_QOP_ASQTAD_FERMION_LINKS( void )
{
  QOP_asqtad_destroy_L(qop_links);
  qop_links = NULL;
}

void
INVALIDATE_FN_LINKS( void )
{
  valid_fn_links = 0;
  valid_fn_links_dmdu0 = 0;
}