/* This module was written by
 *
 * Eduardo Chappa (chappa@washington.edu)
 * http://patches.freeiz.com/alpine/
 *
 *  Original Version: November 1999
 *  Last Modified   : September 14, 2013
 *
 * Send bug reports about this module to the address above.
 */

#include "../pith/headers.h"
#include "../pith/state.h"
#include "../pith/conf.h"
#include "../pith/copyaddr.h"
#include "../pith/mailindx.h"
#include "../pith/rules.h"

#define CSEP_C	('\001')
#define CSEP_S	("\001")

/* Internal Prototypes */

int   test_condition  (CONDITION_S *, int, ENVELOPE *);
int   test_in         (CONDITION_S *, TOKEN_VALUE *, ENVELOPE *, int);
int   test_ni         (CONDITION_S *, TOKEN_VALUE *, ENVELOPE *, int);
int   test_not_in     (CONDITION_S *, TOKEN_VALUE *, ENVELOPE *, int);
int   test_not_ni     (CONDITION_S *, TOKEN_VALUE *, ENVELOPE *, int);
int   test_eq         (CONDITION_S *, TOKEN_VALUE *, ENVELOPE *, int);
int   test_not_eq     (CONDITION_S *, TOKEN_VALUE *, ENVELOPE *, int);
int   isolate_condition (char *, char **, int *);
int   sanity_check_condition (char *);
char  *test_rule      (RULELIST *, int, ENVELOPE *, int *);
char  *trim           (RULEACTION_S *, int, ENVELOPE *);
char  *rextrim        (RULEACTION_S *, int, ENVELOPE *);
char  *raw_value      (RULEACTION_S *, int, ENVELOPE *);
char  *extended_value (RULEACTION_S *, int, ENVELOPE *);
char  *exec_fcn	      (RULEACTION_S *, int, ENVELOPE *);
char  *expand         (char *, void *);
char  *get_name_token (char *);
char  *advance_to_char (char *, char, int, int *);
char  **functions_for_token (char *);
char  *canonicalize_condition (char *, int *);
void  free_token_value (TOKEN_VALUE **);
void  free_condition  (CONDITION_S **);
void  free_condition_value (CONDVALUE_S **);
void  free_ruleaction (RULEACTION_S **);
void  free_rule       (RULE_S **);
void  free_rule_list  (RULELIST **);
void  free_alloc_rule (void **, int);
void  *alloc_mem      (size_t);
void  add_rule        (int, int);
void  set_rule_list    (struct variable *);
void  parse_patterns_into_action(TOKEN_VALUE **);
void  free_parsed_value(TOKEN_VALUE **value);
RULE_S   *parse_rule  (char *, int);
RULELIST *get_rule_list (char **, int, int);
TOKEN_VALUE *parse_group_data (char *,int *);
TOKEN_VALUE *copy_parsed_value (TOKEN_VALUE *, int, ENVELOPE *);
CONDVALUE_S *fill_condition_value (char *);
CONDITION_S *fill_condition (char *);
CONDITION_S *parse_condition (char *, int *);
PRULELIST_S *add_prule        (PRULELIST_S *, PRULELIST_S *);
RULEACTION_S *parse_action (char *, int);

REL_TOKEN rel_rules_test[] = {
   {EQ_REL,     Equal,          test_eq},
   {IN_REL,     Subset,         test_in},
   {NI_REL,     Includes,       test_ni},
   {NOT_EQ_REL, NotEqual,       test_not_eq},
   {NOT_IN_REL, NotSubset,      test_not_in},
   {NOT_NI_REL, NotIncludes,    test_not_ni},
   {NULL,       EndTypes,       NULL}
};

#define NREL  (sizeof(rel_rules_test)/sizeof(rel_rules_test[0]) - 1)

RULE_FCN rule_fcns[] = {
{COPY_FCN,      extended_value, FOR_SAVE|FOR_COMPOSE},
{SAVE_FCN,      extended_value, FOR_SAVE},
{EXEC_FCN,	exec_fcn,	FOR_REPLACE|FOR_TRIM|FOR_RESUB|FOR_COMPOSE},
{REPLY_FCN,     extended_value, FOR_REPLY_INTRO},
{TRIM_FCN,      trim,           FOR_TRIM|FOR_RESUB|FOR_COMPOSE},
{REPLACE_FCN,   extended_value, FOR_REPLACE},
{SORT_FCN,      raw_value,      FOR_SORT},
{INDEX_FCN,     raw_value,      FOR_INDEX},
{COMMAND_FCN,   raw_value,      FOR_KEY},
{REPLYSTR_FCN,  raw_value,      FOR_COMPOSE},
{SIGNATURE_FCN, raw_value,      FOR_COMPOSE},
{RESUB_FCN,     extended_value, FOR_RESUB},
{STARTUP_FCN,   raw_value,      FOR_STARTUP},
{REXTRIM_FCN,   rextrim,        FOR_TRIM|FOR_RESUB|FOR_COMPOSE},
{THRDSTYLE_FCN, raw_value,      FOR_THREAD},
{THRDINDEX_FCN, raw_value,      FOR_THREAD},
{SMTP_FCN,      raw_value,      FOR_COMPOSE},
{NULL,          0,              FOR_NOTHING}
};

char* token_rules[] = {
   FROM_TOKEN,
   NICK_TOKEN,
   OTEXT_TOKEN,
   OTEXTNQ_TOKEN,
   ROLE_TOKEN,
   FOLDER_TOKEN,
   SUBJ_TOKEN,
   PROCID_TOKEN,
   THDDSPSTY_TOKEN,
   THDNDXSTY_TOKEN,
   FLAG_TOKEN,
   COLLECT_TOKEN,
   THDDSPSTY_TOKEN,
   ADDR_TOKEN,
   TO_TOKEN,
   ADDTO_TOKEN,
   ADDCC_TOKEN,
   ADDRECIP_TOKEN,
   SCREEN_TOKEN,
   KEY_TOKEN,
   SEND_TOKEN,
   CC_TOKEN,
   LCC_TOKEN,
   BCC_TOKEN,
   FFROM_TOKEN,
   FADDRESS_TOKEN,
   NULL
};

#define NTOKENS  (sizeof(token_rules)/sizeof(token_rules[0]) - 1)
#define NFCN    (sizeof(rule_fcns)/sizeof(rule_fcns[0]) - 1)

char *subj_fcn[]    = {SUBJ_TOKEN,    REPLACE_FCN, TRIM_FCN, REXTRIM_FCN, EXEC_FCN};
char *from_fcn[]    = {FROM_TOKEN,    REPLACE_FCN, TRIM_FCN, REXTRIM_FCN, EXEC_FCN};
char *otext_fcn[]   = {OTEXT_TOKEN,   REPLACE_FCN, TRIM_FCN, REXTRIM_FCN, EXEC_FCN};
char *otextnq_fcn[] = {OTEXTNQ_TOKEN, REPLACE_FCN, TRIM_FCN, REXTRIM_FCN, EXEC_FCN};

char *adto_fcn[] = {ADDTO_TOKEN, EXEC_FCN, NULL, NULL, NULL};

char **fcns_for_index[] = {subj_fcn, from_fcn, otext_fcn, otextnq_fcn};

#define NFCNFI    (sizeof(fcns_for_index)/sizeof(fcns_for_index[0])) /*for idx*/
#define NFPT      (sizeof(fcns_for_index[0])) /* functions pert token */

SPAREP_S *
get_sparep_for_rule(char *value, int flag)
{
  SPAREP_S *rv;
  rv = (SPAREP_S *) alloc_mem(sizeof(SPAREP_S));
  rv->flag = flag;
  rv->value = value ? cpystr(value) : NULL;
  return rv;
}

void free_sparep_for_rule(void **sparep)
{
  SPAREP_S *spare = (SPAREP_S *) *sparep;
  if(!spare) return;
  if(spare->value)
     fs_give((void **)&spare->value);
  fs_give((void **)sparep);
}


int context_for_function(char *name)
{
  int i, j;
  for (i = 0; i < NFCN && strcmp(rule_fcns[i].name, name); i++);
  return i == NFCN ? 0 : rule_fcns[i].what_for;

}

char **functions_for_token(char *name)
{
  int i;
  for (i = 0; i < NFCNFI && strcmp(fcns_for_index[i][0], name); i++);
  return i == NFCNFI ? NULL : fcns_for_index[i];
}

void
free_alloc_rule (void **voidtext, int code)
{
  switch(code){
     case FREEREGEX : regfree((regex_t *)*voidtext);
			break;
	default: break;
  }
}



void free_token_value(TOKEN_VALUE **token)
{
   if(token && *token){
     if ((*token)->testxt)
	fs_give((void **)&(*token)->testxt);
     free_alloc_rule (&(*token)->voidtxt, (*token)->codefcn);
     if((*token)->next)
	free_token_value(&(*token)->next);
     fs_give((void **)token);
   }
}

void
free_condition_value(CONDVALUE_S **cvalue)
{
  if(cvalue && *cvalue){
    if ((*cvalue)->tname)
	fs_give((void **)&(*cvalue)->tname);
    if ((*cvalue)->value)
	free_token_value(&(*cvalue)->value);
    fs_give((void **)cvalue);
  }
}

void free_condition(CONDITION_S **condition)
{
   if(condition && *condition){
     if((*condition)->cndtype ==  Condition)
	free_condition_value((CONDVALUE_S **)&(*condition)->cndrule);
     else if((*condition)->cndtype ==  ParOpen || (*condition)->cndtype ==  ParClose)
	fs_give(&(*condition)->cndrule);
     if((*condition)->next)
	free_condition(&((*condition)->next));
     fs_give((void **)condition);
   }
}

void free_ruleaction(RULEACTION_S **raction)
{
   if(raction && *raction){
     if ((*raction)->token)
	fs_give((void **)&((*raction)->token));
     if ((*raction)->function)
	fs_give((void **)&((*raction)->function));
     if ((*raction)->value)
	free_token_value(&(*raction)->value);
     fs_give((void **)raction);
   }
}

void free_rule(RULE_S **rule)
{
   if(rule && *rule){
     free_condition(&((*rule)->condition));
     free_ruleaction(&((*rule)->action));
     fs_give((void **)rule);
   }
}

void free_rule_list(RULELIST **rule)
{
  if(!*rule)
    return;

  if((*rule)->next)
    free_rule_list(&((*rule)->next));

  if((*rule)->prule)
    free_rule(&((*rule)->prule));

  fs_give((void **)rule);
}

void
free_parsed_rule_list(PRULELIST_S **rule)
{
  if(!*rule)
    return;

  if((*rule)->next)
    free_parsed_rule_list(&((*rule)->next));

  if((*rule)->rlist)
    free_rule_list(&((*rule)->rlist));

  fs_give((void **)rule);
}

void *
alloc_mem (size_t amount)
{
   void *genmem;
   memset(genmem = fs_get(amount), 0, amount);
   return genmem;
}


void
parse_patterns_into_action(TOKEN_VALUE **tokenp)
{
  if(!*tokenp)
    return;

  if((*tokenp)->testxt){
      regex_t preg;

      (*tokenp)->voidtxt = NULL;
      (*tokenp)->voidtxt = fs_get(sizeof(regex_t));
      if (regcomp((regex_t *)(*tokenp)->voidtxt, 
			(*tokenp)->testxt, REG_EXTENDED) != 0){
         regfree((regex_t *)(*tokenp)->voidtxt);
	 (*tokenp)->voidtxt = NULL;
      }
  }
  if((*tokenp)->voidtxt)
     (*tokenp)->codefcn = FREEREGEX;
  if((*tokenp)->next)
     parse_patterns_into_action(&(*tokenp)->next);
}


int
isolate_condition (char *data, char **cvalue, int  *len)
{
  char *p = data;
  int done = 0, error = 0, next_condition = 0, l;

  if(*p == '"' && p[strlen(p) - 1] == '"'){
    p[strlen(p) - 1] = '\0';
    p++;
  }
  *cvalue = NULL;
  while (*p && !done){
	switch (*p){
	   case '_': *cvalue = advance_to_char(p,'}', STRICTLY, NULL);
		     if(*cvalue){
			strcat(*cvalue,"}");
			p += strlen(*cvalue);
		     }
		     else
			error++;
		     done++;
	   case ' ': p++;
		     break;
	   case '&': 
	   case '|': if (*(p+1) == *p){	/* looking for && or ||*/
			p += 2;
			next_condition++;
		     }
		     else{
			error++;
			done++;
		     }
		     break;
	   case '=': /* looking for => or -> */
	   case '-': if (*(p+1) != '>' || next_condition)
			error++;
		     done++;
		     break;
	   default : done++;
		     error++;
		     break;
	}
  }
  *len = p - data;
  return error ? -1 : (*cvalue ? 1 : 0);
}

TOKEN_VALUE *
parse_group_data (char *data, int *error)
{
  TOKEN_VALUE *rvalue;
  char *p;
  int offset, err = 0;

  if(error)
    *error = 0;

  if (!data)
     return (TOKEN_VALUE *) NULL;

  rvalue = (TOKEN_VALUE *) alloc_mem(sizeof(TOKEN_VALUE));
  if (p = advance_to_char(data,';', STRICTLY, &offset)){
      rvalue->testxt = p;
      data += strlen(p) + 1 + offset;
      rvalue->next   = parse_group_data(data, error);
  }
  else if (p = advance_to_char(data,'}', STRICTLY, NULL))
      rvalue->testxt = p;
  else if (data && *data == '}')
      rvalue->testxt = cpystr("");
  else{
      err++;
      free_token_value(&rvalue);
  }
  if (error)
    *error += err;
  return(rvalue);
}

CONDVALUE_S *
fill_condition_value(char *data)
{
  CONDVALUE_S *condition;
  int i, done, error = 0;
  char *group;

  for (i = 0, done = 0; done == 0 && token_rules[i] != NULL; i++)
      done = strncmp(data,token_rules[i], strlen(token_rules[i])) ? 0 : 1;
  if (done){
     condition = alloc_mem(sizeof(CONDVALUE_S));
     condition->tname = cpystr(token_rules[--i]);
     data += strlen(token_rules[i]);
  }
  else if (*data == '_') {
      char *itokname;
      for (i = 0, done = 0; done == 0 && (itokname = itoken(i)->name) != NULL; i++)
	 done = strncmp(data+1, itokname, strlen(itokname))
			? 0 : data[strlen(itokname) + 1] == '_';
      if (done){
	 condition = (CONDVALUE_S *) alloc_mem(sizeof(CONDVALUE_S));
	 condition->tname = fs_get(strlen(itokname) + 3);
	 sprintf(condition->tname, "_%s_", itokname);
	 data += strlen(itokname) + 2;
      }
      else 
	return NULL;
  } 
  else
     return NULL;

  for (; *data && *data == ' '; data++);
  if (*data){
     for (i = 0, done = 0; done == 0 && rel_rules_test[i].value != NULL; i++)
       done = strncmp(data, rel_rules_test[i].value, 2) ? 0 : 1;
     if (done)
       condition->ttype = rel_rules_test[--i].ttype;
     else{
	 free_condition_value(&condition);
	 return NULL;
     }
  }
  else{
    free_condition_value(&condition);
    return  NULL;
  }

  data += 2;
  for (; *data && *data == ' '; data++);
  if (*data++ != '{'){
     free_condition_value(&condition);
     return NULL;
  }
  group = advance_to_char(data,'}', STRICTLY, &error); 
  if (group || (!group &&  error < 0)){
     condition->value = parse_group_data(data, &error);
     if(group && error)
	free_condition_value(&condition);
     if(group)
        fs_give((void **) &group);
  }
  else
     free_condition_value(&condition);
  return condition;
}

char *
canonicalize_condition(char *data, int *eoc)
{
  char *p = data, *s, *t, c;
  char *q = fs_get((5*strlen(data)+1)*sizeof(char));
  char tmp[10];
  int level, done, error, i;

  if(eoc) *eoc = -1; 	/* assume error */
  *q = '\0';
  if(*p == '"'){
     if(p[strlen(p) - 1] == '"')
	p[strlen(p) - 1] = '\0';
     p++;
  }
  for(level = done = error = 0; *p && !done && !error; ){
     switch(*p){
	case ' ' : p++; break;
	case '(' : strcat(q, CSEP_S); strcat(q, "(");
		   sprintf(tmp, "%d ", level++);
		   strcat(q, tmp);
		   p++;
		   break;
	case ')' : strcat(q, CSEP_S); strcat(q, ")");
		   sprintf(tmp, "%d ", --level);
		   strcat(q, tmp);
		   p++;
		   if(level < 0) error++;
		   break;
	case '_' : for(s = p+1; *s >= 'A' && *s <= 'Z'; s++);
		   for(i = 0; token_rules[i] != NULL; i++)
		      if(!strncmp(token_rules[i], p, s-p))
			break;
		   if(token_rules[i] == NULL)
		     error++;
		   else if(*s++ == '_'){
		     for(; *s == ' '; s++);
		     if(*s && *(s+1)){
			for(i = 0; rel_rules_test[i].value != NULL; i++)
			   if(!strncmp(rel_rules_test[i].value, s, 2))
			      break;
			if (rel_rules_test[i].value == NULL)
			   error++;
			else{
			   s += 2;
			   for(; *s == ' '; s++);
			   if(*s == '{'){
			     if(*(s+1) != '}')
			       t = advance_to_char(s+1,'}', STRICTLY, NULL);
			     else
			       t = cpystr("");
			     if(t != NULL){
			        for(i = 0; t[i] != '\0' && t[i] != CSEP_C; i++);
				if(t[i] == CSEP_C) error++;
			        if(error == 0){
				   strcat(q, CSEP_S); strcat(q, "C[");
				   s += strlen(t) + 1;	/* get past '{' */
				   *s = '\0';
				   strcat(q, p);
				   strcat(q, "}] ");
				   *s++ = '}';
				   p = s;
			        }
				fs_give((void **) &t);
			     }
			     else error++;
			   }
			   else
			     error++;
			}
		     }
		   }
		   else error++;
		   break;
	case '|':
	case '&': if(*(p+1) = *p){
			strcat(q, CSEP_S); strcat(q, *p == '|' ? "OR " : "AND ");
			p += 2;
		  } else error++;
		  break;
	case '-':
	case '=': if (*(p+1) == '>'){
		    if(eoc) *eoc = p - data;
		    done++;
		  }
		  else
		    error++;
		  break;
	default : error++;
		  break;
     }
  }
  if(error || level > 0)	/*simplistic approach by now */
    fs_give((void **)&q);
  else
    q[strlen(q)-1] = '\0';
  return q;
}

/* for a canonical condition, return if it is constructed according
 * to logical rules such as AND or OR between conditions, etc. We assume
 * we already canonicalized data, or else this will not work.
 */
int
sanity_check_condition(char *data)
{
  int i, error;
  char *s, *t, *d;

  if(data == NULL || *data == '\0')	/* no data in, no data out */
    return 0;

  d = fs_get((strlen(data)+1)*sizeof(char));
  for(s = data,i = 0; (t = strchr(s, CSEP_C))!= NULL && (d[i] = *(t+1)); s = t+1, i++);
  d[i] = '\0';
  for(i = 0, error = 0; d[i] != '\0' && error == 0; i++){
     switch(d[i]){
	case 'C': if((d[i+1] != '\0' && (d[i+1] == '(' || d[i+1] == 'C'))
			|| (i == 0 && d[1] != 'A' && d[1] != 'O' && d[1] != '\0'))
		     error++;
		  break;
	case ')': if(i == 0 || d[i+1] != '\0' && (d[i+1] == 'C' || d[i+1] == '('))
		     error++;
		  break;
	case '(': if(d[i+1] == '\0' || d[i+1] == ')' || d[i+1] == 'A' || d[i+1] == 'O')
		     error++;
		  break;
	case 'O':
	case 'A': if(i == 0 || d[i+1] == '\0' || d[i+1] == ')' || d[i+1] == 'A' || d[i+1] == 'O')
		     error++;
		  break;
	default : error++;
     }
  }
  if(d) fs_give((void **)&d);
  return error ? 0 : 1;
}

/* given a parsed data that satisfies sanity checks, parse it
 * into a condition we can check later on.
 */
CONDITION_S *
fill_condition(char *data)
{
  char *s, *t, *u;
  CONDITION_S *rv = NULL;
  CONDVALUE_S *cvalue;
  int *i;

  if(data == NULL || *data == '\0' || (s = strchr(data, CSEP_C)) == NULL)
    return NULL;

  rv = (CONDITION_S *) alloc_mem(sizeof(CONDITION_S));
  switch(*++s){
     case ')':
     case '(':	 i = fs_get(sizeof(int));
		*i = atoi(s+1);
		rv->cndrule = (void *) i;
		rv->cndtype = *s == '(' ? ParOpen : ParClose;
		break;

     case 'C':	if((u = strchr(s+2, CSEP_C)) != NULL){
		   *u = '\0';
		   t = strrchr(s, ']');
		   t = '\0';
		   *u = CSEP_C;
		} else
		   s[strlen(s) - 1] = '\0';
		rv->cndrule = (void *) fill_condition_value(s+2);
		rv->cndtype = Condition;
		break;

     case 'A':
     case 'O':  rv->cndtype = *s == 'A' ? And : Or;
		break;

     default : fs_give((void **)&rv);
	       break;
  }
  rv->next = fill_condition(strchr(s, CSEP_C));

  return rv;
} 

/* eoc = end of condition, equal to -1 on error */
CONDITION_S *
parse_condition (char *data, int *eoc)
{
  CONDITION_S *condition = NULL;
  char *pvalue;
  
  if((pvalue = canonicalize_condition(data, eoc)) != NULL
	&& sanity_check_condition(pvalue) > 0)
    condition = fill_condition(pvalue);

  if(pvalue)
    fs_give((void **)&pvalue);

  if (condition == NULL && eoc)
    *eoc = -1;

  return condition;
}

RULEACTION_S *
parse_action (char *data, int context)
{
  int i, done, is_save;
  RULEACTION_S *raction = NULL;
  char *function, *p = data;

  if (p == NULL || *p == '\0')
     return NULL;

  is_save = *p == '-';
  p += 2;
  for (; *p == ' '; p++);

  if (is_save){	/* got "->", a save-rule separator */
     raction = (RULEACTION_S *) alloc_mem(sizeof(RULEACTION_S));
     raction->function = cpystr("_SAVE_");
     raction->value    = (TOKEN_VALUE *) alloc_mem(sizeof(TOKEN_VALUE));
     raction->context |= FOR_SAVE;
     raction->exec     = extended_value;
     raction->value->testxt = cpystr(p);
     return raction;
  }
  for (i = 0, done = 0; !done && (i < NFCN); i++)
       done = (strstr(p,rule_fcns[i].name) == p);
  p += done ? strlen(rule_fcns[--i].name) + 1 : 0;
  if(!*p || (rule_fcns[i].what_for && !(rule_fcns[i].what_for & context)))
     return NULL;
  if (done){
     raction = alloc_mem(sizeof(RULEACTION_S));
	/* We assign raction->token to be subject. This is not necessary for
	   most rules. It is done only for rules that need it and will not
	   make any difference in rules that do not need it. It will hopefully
	   reduce complexity in the language
	 */
     raction->token    = cpystr(SUBJ_TOKEN);
     raction->function = cpystr(rule_fcns[i].name);
     raction->context  = rule_fcns[i].what_for;
     raction->exec     = rule_fcns[i].execute;
     raction->value    = (TOKEN_VALUE *) alloc_mem(sizeof(TOKEN_VALUE));
     raction->value->testxt = advance_to_char(p,'}', STRICTLY, NULL);
     if(!raction->value->testxt)
       free_ruleaction(&raction);
     return raction;
  }

  done = (((function = strstr(p, "_TRIM_")) != NULL)
	  ? 1 : ((function = strstr(p, "_COPY_")) != NULL)
	  ? 2 : ((function = strstr(p, "_EXEC_")) != NULL)
	  ? 3 : ((function = strstr(p, "_REXTRIM_")) != NULL)
	  ? 4 : ((function = strstr(p, "_REPLACE_")) != NULL)
	  ? 5 : 0);

  if(!function)
     return (RULEACTION_S *) NULL;

  *function = '\0';
   raction = (RULEACTION_S *) alloc_mem(sizeof(RULEACTION_S));
   raction->token = get_name_token(p);
  *function = '_';
   p += strlen(raction->token) + 1;
   for (; *p && *p == ' '; p++);
   if (!strncmp(p, ":=", 2))
      p += 2;
   else{
      free_ruleaction(&raction);
      return NULL;
   }
   for (; *p && *p == ' '; p++);
   if (p != function){
      free_ruleaction(&raction);
      return NULL;
   }
   p += done <= 3 ? 6 : 9; /* 6 = strlen("_EXEC_"), 9 = strlen("_REPLACE_") */
   if (*p != '{'){
      free_ruleaction(&raction);
      return NULL;
   }
   *p = '\0';
   for(i = 0; i < NFCN && strcmp(function, rule_fcns[i].name);i++);
   raction->function   = cpystr(function);
   raction->is_trim    = strcmp(function,"_TRIM_")    ? 0 : 1;
   raction->is_rextrim = strcmp(function,"_REXTRIM_") ? 0 : 1;
   raction->is_replace = strcmp(function,"_REPLACE_") ? 0 : 1;
   raction->context    = rule_fcns[i].what_for;
   raction->exec       = rule_fcns[i].execute;
   *p++ = '{';
   if((raction->value = parse_group_data(p, NULL)) == NULL 
	|| raction->value->testxt == NULL)
      free_ruleaction(&raction);
   if(raction && raction->is_rextrim)
      parse_patterns_into_action(&raction->value);
   return raction;
}

RULE_S *
parse_rule (char *data, int context)
{
  RULE_S *prule;	/*parsed rule */
  int len = 0;
  
  if (!(prule = (RULE_S *) alloc_mem(sizeof(RULE_S))) ||
	!(prule->condition = parse_condition(data, &len)) ||
	!(prule->action = parse_action(data+len, context)))
      free_rule(&prule);

  return prule;
}

RULELIST *
get_rule_list(char **list, int context, int i)
{
  RULE_S *rule;
  RULELIST *trulelist = NULL;

  if (list[i] && *list[i]){
     if(rule = parse_rule(list[i], context)){
	trulelist  = (RULELIST *)alloc_mem(sizeof(RULELIST));
	trulelist->prule = rule;
	trulelist->next = get_rule_list(list, context, i+1);
     }
     else
	trulelist = get_rule_list(list, context, i+1);
  }
  return trulelist;
}

PRULELIST_S *
add_prule(PRULELIST_S *rule_list, PRULELIST_S *rule)
{
   if (!rule_list)
      rule_list = (PRULELIST_S *) alloc_mem(sizeof(PRULELIST_S));

   if(rule_list->next)
     rule_list->next = add_prule(rule_list->next, rule);
   else{
     if (rule_list->rlist)
	rule_list->next = rule;
     else
	rule_list = rule;
   }
   return rule_list;
}  

void
add_rule(int code, int context)
{
  char **list = ps_global->vars[code].current_val.l;
  PRULELIST_S *prulelist, *trulelist, *orulelist;

  if (list && *list && **list){
     trulelist = (PRULELIST_S *)alloc_mem(sizeof(PRULELIST_S));
     trulelist->varnum = code;
     if (trulelist->rlist = get_rule_list(list, context, 0))
        ps_global->rule_list = add_prule(ps_global->rule_list, trulelist);
     else
	free_parsed_rule_list(&trulelist);
  }
}

/* see create_rule_list below */
void
set_rule_list(struct variable *vars)
{
    set_current_val(&vars[V_THREAD_DISP_STYLE_RULES], TRUE, TRUE);
    set_current_val(&vars[V_THREAD_INDEX_STYLE_RULES], TRUE, TRUE);
    set_current_val(&vars[V_COMPOSE_RULES], TRUE, TRUE);
    set_current_val(&vars[V_FORWARD_RULES], TRUE, TRUE);
    set_current_val(&vars[V_INDEX_RULES], TRUE, TRUE);
    set_current_val(&vars[V_KEY_RULES], FALSE, TRUE);
    set_current_val(&vars[V_REPLACE_RULES], TRUE, TRUE);
    set_current_val(&vars[V_REPLY_INDENT_RULES], TRUE, TRUE);
    set_current_val(&vars[V_REPLY_LEADIN_RULES], TRUE, TRUE);
    set_current_val(&vars[V_RESUB_RULES], TRUE, TRUE);
    set_current_val(&vars[V_SAVE_RULES], TRUE, TRUE);
    set_current_val(&vars[V_SMTP_RULES], TRUE, TRUE);
    set_current_val(&vars[V_SORT_RULES], TRUE, TRUE);
    set_current_val(&vars[V_STARTUP_RULES], TRUE, TRUE);
}

/* see set_rule_list above */
void
create_rule_list(struct variable *vars)
{
  set_rule_list(vars);
  add_rule(V_THREAD_DISP_STYLE_RULES, FOR_THREAD);
  add_rule(V_THREAD_INDEX_STYLE_RULES, FOR_THREAD);
  add_rule(V_COMPOSE_RULES, FOR_COMPOSE);
  add_rule(V_FORWARD_RULES, FOR_COMPOSE);
  add_rule(V_INDEX_RULES, FOR_INDEX);
  add_rule(V_KEY_RULES, FOR_KEY);
  add_rule(V_REPLACE_RULES, FOR_REPLACE);
  add_rule(V_REPLY_INDENT_RULES, FOR_COMPOSE);
  add_rule(V_REPLY_LEADIN_RULES, FOR_REPLY_INTRO);
  add_rule(V_RESUB_RULES, FOR_RESUB|FOR_TRIM);
  add_rule(V_SAVE_RULES, FOR_SAVE);
  add_rule(V_SMTP_RULES, FOR_COMPOSE);
  add_rule(V_SORT_RULES, FOR_SORT);
  add_rule(V_STARTUP_RULES, FOR_STARTUP);
}

int
condition_contains_token(CONDITION_S *condition, char *token)
{
  while(condition && condition->cndtype != Condition)
      condition =  condition->next;

  return condition 
	  ? (!strcmp(COND(condition)->tname, token) 
		? 1
		: condition_contains_token(condition->next, token)) 
	  : 0;
}

RULELIST *
get_rulelist_from_code(int code, PRULELIST_S *list)
{
  return list ? (list->varnum == code ? list->rlist 
			      : get_rulelist_from_code(code, list->next))
	      : (RULELIST *) NULL;
}   

char *
test_rule(RULELIST *rlist, int ctxt, ENVELOPE *env, int *n)
{
  char *result;

  if(!rlist)
     return NULL;

  if (result = process_rule(rlist->prule, ctxt, env))
      return result;
  else{
       (*n)++;
       return test_rule(rlist->next, ctxt, env, n);
  } 
}

RULE_S *
get_rule (RULELIST *rule, int n)
{
  return rule ? (n ? get_rule(rule->next, n-1) : rule->prule) 
	      : NULL;
}

/* get_result_rule:
 * Parameters: list: the list of rules to be passed to the function to check
 *             rule_context: context of the rule
 *             env : envelope used to check the rule, if needed.
 *
 * Returns: The value of the first rule that is satisfied in the list, or
 *          NULL if not. This function should be called in the following 
 *          way (notice that memory is freed by caller).
 *
 * You should use this function to obtain the result of a rule. You can
 * also call directly "process_rule", but I advice to use this function if
 * there's no difference on which function to call.

   RULE_RESULT *rule;

   rule = (RULE_RESULT *) 
           get_result_rule(V_SOME_RULE, context, envelope);

   if (rule){ 
       assign the value of rule->result;
       if (rule->result)
          fs_give((void **)&rule->result);
       fs_give((void **)&rule);
   }
 */

RULE_RESULT *
get_result_rule(int code, int rule_context, ENVELOPE *env)
{
    char  *rule_result;
    RULE_RESULT *rule = NULL;
    RULELIST *rlist;
    int n = 0;

    if(!(rule_context & FOR_RULE))
      rule_context |= FOR_RULE;
    rlist = get_rulelist_from_code(code, ps_global->rule_list);
    if (rlist){
       rule_result = test_rule(rlist, rule_context, env, &n);
       if (rule_result && *rule_result){
          rule = (RULE_RESULT *) fs_get (sizeof(RULE_RESULT));
          rule->result = rule_result;
          rule->number = n;
       }
    }
    return rule;
}

char *get_rule_result(int rule_context, char *newfolder, int code)
{   
    char        *rule_result = NULL;
    ENVELOPE    *news_envelope;
    RULE_RESULT *rule;

    if (IS_NEWS(ps_global->mail_stream)){
       news_envelope = mail_newenvelope();
       news_envelope->newsgroups = cpystr(newfolder);
    }
    else
       news_envelope = NULL;

    rule = get_result_rule(code, rule_context, news_envelope);

    if (news_envelope)
        mail_free_envelope (&news_envelope);

    if (rule){
        rule_result = cpystr(rule->result);
        if (rule->result)
          fs_give((void **)&rule->result);
        fs_give((void **)&rule);
    }
    return rule_result;
}

/* process_rule:
   Parameters:  prule, a processed rule, ready to be tested
		rule_context: context of the rule, and
		env: An envelope if needed.

   Returns   :  The value of the processed rule_data if the processing was 
		successful and matches context and possibly the envelope, or
		NULL if there's no match
 */

char *
process_rule (RULE_S *prule, int rule_context, ENVELOPE *env)
{
   if(!prule)
     return NULL;

   if(!(rule_context & FOR_RULE))
      rule_context |= FOR_RULE;

   return test_condition(prule->condition, rule_context, env)
	    ? (prule->action->exec)(prule->action, rule_context, env)
	    : NULL;
}

TOKEN_VALUE *
copy_parsed_value(TOKEN_VALUE *value, int ctxt, ENVELOPE *env)
{
   TOKEN_VALUE *tval = NULL;

   if(!value)
      return NULL;

   if(value->testxt){
     tval = (TOKEN_VALUE *) alloc_mem(sizeof(TOKEN_VALUE));
     tval->testxt = detoken_src(value->testxt, ctxt, env, NULL, NULL, NULL);
     tval->voidtxt = value->voidtxt;
     tval->codefcn = value->codefcn;     
   }
   if(value->next)
     tval->next = copy_parsed_value(value->next, ctxt, env);

   return tval;
}

void
free_parsed_value(TOKEN_VALUE **value)
{
   TOKEN_VALUE *tval = NULL;

   if(!*value)
      return;

   if((*value)->testxt)
     fs_give((void **)&(*value)->testxt);

   if((*value)->next)
     free_parsed_value(&(*value)->next);

    fs_give((void **)value);
}

int
test_condition_work(CONDITION_S *bc, CONDITION_S *ec, int rcntxt, ENVELOPE *env)
{
   int rv,level;
   TOKEN_VALUE *group;
   CONDITION_S *cend;

   switch(bc->cndtype){
	case Condition:	group = copy_parsed_value(COND(bc)->value, rcntxt, env);
			rv = (*rel_rules_test[COND(bc)->ttype].execute)(bc, group, env, rcntxt);
			free_parsed_value(&group);
			if(bc == ec)
			  return rv;
			if(bc->next == NULL)
			  return rv;
			else
			  switch(bc->next->cndtype){
			    case And: return rv ? test_condition_work(bc->next->next, ec, rcntxt, env) : 0;
				  break;
			    case Or : return rv ? 1 : test_condition_work(bc->next->next, ec, rcntxt, env);
				  break;
			    case ')': return rv;
			    default : rv = 0; break;  /* fail, we should not be here */
			  }
			break;

	case ParOpen:	level = ((int *)bc->cndrule)[0];
			for(cend = bc; cend->next && (cend->next->cndtype != ParClose
				  	|| ((int *)cend->next->cndrule)[0] != level); 
					cend = cend->next);
			rv = test_condition_work(bc->next, cend, rcntxt, env);
			cend = cend->next;	/* here we are at ')' */
			if(cend->next == NULL)
			  return rv;
			else{
			  switch(cend->next->cndtype){
			    case And: return rv ? test_condition_work(cend->next->next, ec, rcntxt, env) : 0;
				  break;
			    case Or : return rv ? 1 : test_condition_work(cend->next->next, ec, rcntxt, env);
				  break;
			    default : rv = 0; break;	/* fail, we should not be here */
			  }
			}
			break;
	     default:	rv = 0; break; 	/* fail, we should not be here */
   }
   return rv;  /* we never ever get here */
}


int
test_condition(CONDITION_S *condition, int rcntxt, ENVELOPE *env)
{
   return test_condition_work(condition, NULL, rcntxt, env);
}

/* returns the name of the token it found or NULL if there is no token, the
 * real value of the token is obtained by calling the detoken_src function.
 */ 

char *
get_name_token (char *condition)
{
  char *p = NULL, *q, *s;

    if ((q = strchr(condition,'_')) && (s = strchr(q+1,'_'))){
	char c = *++s;
	*s = '\0';
	 p = cpystr(q);
	*s = c;
    }
    return p;
}

/* This function tests if a string contained in the variable "group" is
 * in the "condition"
 */
int test_in (CONDITION_S *condition, TOKEN_VALUE *group, ENVELOPE *env, 
		int context)
{
 int rv = 0;
 char *test;
 TOKEN_VALUE *test_group = group;

 test = env && env->sparep && ((SPAREP_S *)env->sparep)->flag & USE_RAW_SP
	? cpystr(((SPAREP_S *)env->sparep)->value)
	: detoken_src(COND(condition)->tname, context, env, NULL, NULL, NULL);
 if (test){
    while (rv == 0 && test_group){
       if(!*test || strstr(test_group->testxt, test))
	  rv++;
       else
	  test_group = test_group->next;
    }
    fs_give((void **)&test);
 }
 return rv;
}

int test_ni (CONDITION_S *condition, TOKEN_VALUE *group, 
		ENVELOPE *env, int context)
{
 int rv = 0;
 char *test;
 TOKEN_VALUE *test_group = group;

 test = env && env->sparep && ((SPAREP_S *)env->sparep)->flag & USE_RAW_SP
	? cpystr(((SPAREP_S *)env->sparep)->value)
	: detoken_src(COND(condition)->tname, context, env, NULL, NULL, NULL);
 if (test){
    if(!test_group)
      rv++;
    while (rv == 0 && test_group){
       if(!*test_group->testxt || strstr(test, test_group->testxt))
	  rv++;
       else
	  test_group = test_group->next;
    }
    fs_give((void **)&test);
 }
 return rv;
}

int test_not_in (CONDITION_S *condition, TOKEN_VALUE *group, 
		ENVELOPE *env, int context)
{
 return !test_in(condition, group, env, context);
}

int test_not_ni (CONDITION_S *condition, TOKEN_VALUE *group, 
		ENVELOPE *env, int context)
{
 return !test_ni(condition, group, env, context);
}

int test_eq (CONDITION_S *condition, TOKEN_VALUE *group, 
		ENVELOPE *env, int context)
{
 int rv = 0;
 char *test;
 TOKEN_VALUE *test_group = group;

 test = env && env->sparep && ((SPAREP_S *)env->sparep)->flag & USE_RAW_SP
	? cpystr(((SPAREP_S *)env->sparep)->value)
	: detoken_src(COND(condition)->tname, context, env, NULL, NULL, NULL);
 if (test){
    while (rv == 0 && test_group){
       if((!*test && !*test_group->testxt) || !strcmp(test_group->testxt, test))
	  rv++;
       else
	  test_group = test_group->next;
    }
    fs_give((void **)&test);
 }
 return rv;
}

int test_not_eq (CONDITION_S *condition, TOKEN_VALUE *group, 
		ENVELOPE *env, int context)
{
 return !test_eq(condition, group, env, context);
}

char *
do_trim (char *test, TOKEN_VALUE *tval)
{
   char *begin_text;
   int offset = 0;

   if (!tval)
      return test;

   while(begin_text = strstr(test+offset,tval->testxt)){
      strcpy(begin_text, begin_text+strlen(tval->testxt));
      offset = begin_text - test;
   }

   return do_trim(test, tval->next);
}

char *
trim (RULEACTION_S *action, int context, ENVELOPE *env)
{
 char *begin_text, *test;
 RULEACTION_S *taction = action;
 int offset;

 if (taction->context & context){
    if (test = detoken_src(taction->token, context, env, NULL, NULL, NULL))
       test = do_trim(test, taction->value);
    return test;
 }
 return NULL;
}


char *
do_rextrim (char *test, TOKEN_VALUE *tval)
{
   char *begin_text, *trim_text;
   int offset = 0;

   if (!tval)
      return test;

   trim_text = expand(test, tval->voidtxt);
   while(trim_text && (begin_text = strstr(test+offset,trim_text))){
      strcpy(begin_text, begin_text+strlen(trim_text));
      offset = begin_text - test;
   }

   return do_rextrim(test, tval->next);
}

char *
rextrim (RULEACTION_S *action, int context, ENVELOPE *env)
{
 char *test = NULL;
 RULEACTION_S *taction = action;

 if (taction->context & context &&
    (test = detoken_src(taction->token, context, env, NULL, NULL, NULL)))
	test = do_rextrim(test, taction->value);
 return test;
}

char *
raw_value (RULEACTION_S *action, int context, ENVELOPE *env)
{
return (action->context & context) ? cpystr(action->value->testxt) : NULL;
}

char *
extended_value (RULEACTION_S *action, int ctxt, ENVELOPE *env)
{
return (action->context & ctxt) 
	? detoken_src(action->value->testxt, ctxt, env, NULL, NULL, NULL)
	: NULL;
}

/* advances given_string until it finds given_char, memory freed by caller  */
char *
advance_to_char(char *given_string, char given_char, int flag, int *error) 
{
   char *b, *s, c;
   int i, err  = 0, quoted ;

   if (error)
      *error = 0;

   if (!given_string || !*given_string)
       return NULL;

   b = s = cpystr(given_string);
   for(i = 0, quoted = 0, c = *s; c ; c = *++s){
      if(c == '\\'){
	 quoted++;
	 continue;
      }
      if(quoted){
	quoted = 0;
	if (c == given_char){
	  err += flag & STRICTLY ? 0 : 1;
	  err++;
	  break;
	}
	b[i++] = '\\';
      }
      if(c == given_char){
	 err += flag & STRICTLY ? 0 : 1;
	 break;
      }
      b[i++] = c;
   }
   b[i] = '\0';
   if (b && (strlen(b) == strlen(given_string)) && (flag & STRICTLY)){
      fs_give((void **)&b);
      return NULL;   /* character not found */
   }

   if(b && !*b){
     fs_give((void **)&b);
     err = -1;
   }

   if (error)
      *error = err;

   return b;
}

/* Regular Expressions Support */
char *
expand (char *string, void *pattern)
{
 char c, *ret_string = NULL;
 regmatch_t pmatch;
 
  if((regex_t *)pattern == NULL)
     return NULL;

  if(regexec((regex_t *)pattern, string , 1, &pmatch, 0) == 0 
	&& pmatch.rm_so < pmatch.rm_eo){
      c = string[pmatch.rm_eo];
      string[pmatch.rm_eo] = '\0';
      ret_string = cpystr(string+pmatch.rm_so);
      string[pmatch.rm_eo] = c;
  }
  return ret_string;
}


char *
exec_fcn (RULEACTION_S *action, int ctxt, ENVELOPE *env)
{
  STORE_S *output_so;
  gf_io_t	gc, pc;
  char *status, *rv, *cmd, *test;

  if(!(action->context & ctxt))
    return NULL;

  if((test = detoken_src(action->token, ctxt, env, NULL, NULL, NULL)) != NULL)
    gf_set_readc(&gc, test, (unsigned long)strlen(test), CharStar, 0);

  if((output_so = so_get(CharStar, NULL, EDIT_ACCESS)) != NULL)
     gf_set_so_writec(&pc, output_so);

  cmd = (char *)fs_get((strlen(action->value->testxt) + strlen("_TMPFILE_") + 2)*sizeof(char));
  sprintf(cmd,"%s _TMPFILE_", action->value->testxt);
  status = (*ps_global->tools.exec_rule)(cmd, gc, pc);

  so_seek(output_so, 0L, 0);
  rv = cpystr(output_so->dp);
  gf_clear_so_writec(output_so);
  so_give(&output_so);
  if(test)
    fs_give((void **)&test);

  return status ? NULL : rv;
}

ENVELOPE *
rules_fetchenvelope(INDEXDATA_S *idata, int *we_clear)
{
  ENVELOPE *env;

  if (idata->no_fetch){
     if(we_clear)
        *we_clear = 1;
    env = mail_newenvelope();
    env->from     = copyaddrlist(idata->from);
    env->to       = copyaddrlist(idata->to);
    env->cc       = copyaddrlist(idata->cc);
    env->sender   = copyaddrlist(idata->sender);
    env->subject  = cpystr(idata->subject);
    env->date     = cpystr((unsigned char *) idata->date);
    env->newsgroups = cpystr(idata->newsgroups);
    return env;
  }
  if(we_clear)
     *we_clear = 0;
  env = pine_mail_fetchenvelope(idata->stream, idata->rawno);
  return env;
}
