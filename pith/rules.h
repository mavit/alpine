/* Included file rules.h */

#ifndef PITH_RULES_INCLUDED
#define PITH_RULES_INCLUDED

#include "../pith/conftype.h"
#include "../pith/detoken.h"
#include "../pith/indxtype.h"
#include "../pith/rulestype.h"

/* Exported prototypes */

void  create_rule_list (struct variable *);
SPAREP_S *get_sparep_for_rule(char *, int);
void  free_sparep_for_rule(void **);
void  free_parsed_rule_list (PRULELIST_S **);
RULE_RESULT *get_result_rule (int, int, ENVELOPE *);
char  *get_rule_result (int , char *, int);
char  *process_rule   (RULE_S *, int, ENVELOPE *);
char  **functions_for_token (char *);
RULELIST *get_rulelist_from_code (int, PRULELIST_S *);
RULE_S   *get_rule  (RULELIST *, int);
int   condition_contains_token (CONDITION_S *, char *);
int   context_for_function (char *);
ENVELOPE *rules_fetchenvelope(INDEXDATA_S *idata, int *we_clear);

/* Separators:
 *
 * A separator is a string that separates the rule condition with the rule
 * action. Below is the list of separators
 *
 */

#define  SAVE_TO_SEP  "->"
#define  APPLY_SEP    "=>"

/*------- Definitions of tokens -------*/
/*------ Keep the list alphabetically sorted, thanks -------*/

#define ADDR_TOKEN	"_ADDRESS_"
#define ADDCC_TOKEN	"_ADDRESSCC_"
#define ADDRECIP_TOKEN	"_ADDRESSRECIPS_"
#define ADDTO_TOKEN	"_ADDRESSTO_"
#define BCC_TOKEN	"_BCC_"
#define CC_TOKEN	"_CC_"
#define COLLECT_TOKEN	"_COLLECTION_"
#define FLAG_TOKEN	"_FLAG_"
#define FOLDER_TOKEN	"_FOLDER_"
#define FADDRESS_TOKEN	"_FORWARDADDRESS_"
#define FFROM_TOKEN	"_FORWARDFROM_"
#define FROM_TOKEN	"_FROM_"
#define KEY_TOKEN	"_PKEY_"
#define LCC_TOKEN	"_LCC_"
#define NICK_TOKEN	"_NICK_"
#define OTEXT_TOKEN	"_OPENINGTEXT_"
#define OTEXTNQ_TOKEN	"_OPENINGTEXTNQ_"
#define PROCID_TOKEN	"_PROCID_"
#define ROLE_TOKEN	"_ROLE_"
#define SCREEN_TOKEN	"_SCREEN_"
#define SEND_TOKEN	"_SENDER_"
#define SUBJ_TOKEN	"_SUBJECT_"
#define THDDSPSTY_TOKEN	"_THREADSTYLE_"
#define THDNDXSTY_TOKEN	"_THREADINDEX_"
#define TO_TOKEN	"_TO_"

/*------ Definitions of relational operands -------------*/

typedef struct {
        char       *value;
	TestType    ttype;
        int        (*execute)();
} REL_TOKEN;

/* Relational Operands */
#define AND_REL     "&&"        /* For putting more than one condition  */
#define IN_REL      "<<"        /* For belonging relation */
#define NI_REL      ">>"        /* For contain relation   */
#define NOT_IN_REL  "!<"        /* Negation of IN_REL   */
#define NOT_NI_REL  "!>"        /* Negation of NI_REL   */
#define EQ_REL      "=="        /* Test of equality     */
#define NOT_EQ_REL  "!="        /* Test of inequality   */
#define OPEN_SET    "{"         /* Braces to open a set */
#define CLOSE_SET   "}"         /* Braces to close a set*/

/*--- Context in which these variables can be used ---*/

typedef struct use_context {
    char        *name;
    int          what_for;
} USE_IN_CONTEXT;


static USE_IN_CONTEXT tokens_use[] = {
    {NICK_TOKEN,	FOR_SAVE},
    {FROM_TOKEN,	FOR_SAVE},
    {OTEXT_TOKEN,	FOR_SAVE|FOR_FOLDER},
    {OTEXTNQ_TOKEN,	FOR_SAVE|FOR_FOLDER},
    {ROLE_TOKEN,	FOR_COMPOSE},
    {FOLDER_TOKEN,	FOR_SAVE|FOR_FOLDER|FOR_THREAD|FOR_COMPOSE},
    {SUBJ_TOKEN,	FOR_SAVE|FOR_FOLDER|FOR_COMPOSE},
    {FLAG_TOKEN,	FOR_SAVE|FOR_FLAG},
    {COLLECT_TOKEN,	FOR_SAVE|FOR_COMPOSE|FOR_FOLDER|FOR_THREAD},
    {THDDSPSTY_TOKEN,	FOR_THREAD},
    {THDNDXSTY_TOKEN,	FOR_THREAD},
    {ADDR_TOKEN,	FOR_SAVE|FOR_FOLDER},
    {TO_TOKEN,		FOR_SAVE},
    {ADDTO_TOKEN,	FOR_SAVE|FOR_COMPOSE},
    {ADDCC_TOKEN,	FOR_SAVE|FOR_COMPOSE},
    {ADDRECIP_TOKEN,	FOR_SAVE|FOR_COMPOSE},
    {SCREEN_TOKEN,	FOR_KEY},
    {KEY_TOKEN,		FOR_KEY},
    {SEND_TOKEN,	FOR_SAVE},
    {CC_TOKEN,		FOR_SAVE},
    {BCC_TOKEN,		FOR_COMPOSE},
    {LCC_TOKEN,		FOR_COMPOSE},
    {FFROM_TOKEN,	FOR_COMPOSE},
    {FADDRESS_TOKEN,	FOR_COMPOSE},
    {NULL,		FOR_NOTHING}
};


typedef struct {
        char         *name;
        char*        (*execute)();
        int          what_for;
} RULE_FCN;

#define COMMAND_FCN	"_COMMAND_"
#define COPY_FCN	"_COPY_"
#define EXEC_FCN	"_EXEC_"
#define INDEX_FCN       "_INDEX_"
#define REPLACE_FCN     "_REPLACE_"
#define REPLYSTR_FCN    "_RESTR_"
#define REPLY_FCN       "_REPLY_"
#define RESUB_FCN       "_RESUB_"
#define REXTRIM_FCN	"_REXTRIM_"
#define SAVE_FCN        "_SAVE_"
#define SIGNATURE_FCN   "_SIGNATURE_"
#define SMTP_FCN        "_SMTP_"
#define SORT_FCN        "_SORT_"
#define STARTUP_FCN     "_STARTUP_"
#define THRDSTYLE_FCN   "_THREADSTYLE_"
#define THRDINDEX_FCN   "_THREADINDEX_"
#define TRIM_FCN        "_TRIM_"

#define STRICTLY  0x1
#define RELAXED 0x2

#endif 	/* PITH_RULES_INCLUDED */
