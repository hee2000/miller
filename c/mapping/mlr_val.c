#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "lib/mlr_globals.h"
#include "mapping/mlr_val.h"

// For some Linux distros, in spite of including time.h:
char *strptime(const char *s, const char *format, struct tm *tm);

// ----------------------------------------------------------------
mv_t MV_NULL = {
	.type = MT_NULL,
	.u.intv = 0
};
mv_t MV_ERROR = {
	.type = MT_ERROR,
	.u.intv = 0
};

// ----------------------------------------------------------------
char* mt_describe_type(int type) {
	switch (type) {
	case MT_NULL:   return "T_NULL";   break;
	case MT_ERROR:  return "T_ERROR";  break;
	case MT_BOOL:   return "T_BOOL";   break;
	case MT_DOUBLE: return "T_DOUBLE"; break;
	case MT_INT:    return "T_INT";    break;
	case MT_STRING: return "T_STRING"; break;
	default:        return "???";      break;
	}
}

// xxx cmt mem-mgt
// xxx put "alloc" in the name
char* mt_format_val(mv_t* pval) {
	char* string = NULL;
	switch(pval->type) {
	case MT_NULL:
		return strdup("");
		break;
	case MT_ERROR:
		return strdup("(error)");
		break;
	case MT_BOOL:
		return strdup(pval->u.boolv ? "true" : "false");
		break;
	case MT_DOUBLE:
		// xxx what is worst-case here ...
		string = mlr_malloc_or_die(32);
		sprintf(string, MLR_GLOBALS.ofmt, pval->u.dblv);
		return string;
		break;
	case MT_INT:
		// log10(2**64) is < 20 so this is plenty.
		string = mlr_malloc_or_die(32);
		sprintf(string, "%lld", pval->u.intv);
		return string;
		break;
	case MT_STRING:
		return strdup(pval->u.strv);
		break;
	default:
		return strdup("???");
		break;
	}
}

char* mt_describe_val(mv_t val) {
	char* stype = mt_describe_type(val.type);
	char* strv  = mt_format_val(&val);
	char* desc  = mlr_malloc_or_die(strlen(stype) + strlen(strv) + 4);
	strcpy(desc, "[");
	strcat(desc, stype);
	strcat(desc, "] ");
	strcat(desc, strv);
	return desc;
}

// ----------------------------------------------------------------
int mt_get_boolean_strict(mv_t* pval) {
	if (pval->type != MT_BOOL) {
		char* desc = mt_describe_type(pval->type);
		fprintf(stderr, "Expression does not evaluate to boolean: got %s.\n", desc);
		free(desc);
		exit(1);
	}
	return pval->u.boolv;
}

// ----------------------------------------------------------------
// xxx check for semantics comparable to mt_get_boolean_strict
void mt_get_double_strict(mv_t* pval) {
	if (pval->type == MT_NULL)
		return;
	if (pval->type == MT_ERROR)
		return;
	if (pval->type == MT_DOUBLE)
		return;
	if (pval->type == MT_STRING) {
		double dblv;
		if (!mlr_try_double_from_string(pval->u.strv, &dblv)) {
			pval->type = MT_ERROR;
			pval->u.intv = 0;
		} else {
			pval->type = MT_DOUBLE;
			pval->u.dblv = dblv;
		}
	} else if (pval->type == MT_INT) {
		pval ->type = MT_DOUBLE;
		pval->u.dblv = (double)pval->u.intv;
	} else if (pval->type == MT_BOOL) {
		pval->type = MT_ERROR;
		pval->u.intv = 0;
	}
	// xxx else panic
}

// ----------------------------------------------------------------
// xxx merge with mt_get_double_string w/ a nullable parameter
void mt_get_double_nullable(mv_t* pval) {
	if (pval->type == MT_NULL)
		return;
	if (pval->type == MT_ERROR)
		return;
	if (pval->type == MT_DOUBLE)
		return;
	if (pval->type == MT_STRING) {
		double dblv;
		if (*pval->u.strv == '\0') {
			pval->type = MT_NULL;
			pval->u.intv = 0;
		} else if (!mlr_try_double_from_string(pval->u.strv, &dblv)) {
			pval->type = MT_ERROR;
			pval->u.intv = 0;
		} else {
			pval->type = MT_DOUBLE;
			pval->u.dblv = dblv;
		}
	} else if (pval->type == MT_INT) {
		pval ->type = MT_DOUBLE;
		pval->u.dblv = (double)pval->u.intv;
	} else if (pval->type == MT_BOOL) {
		pval->type = MT_ERROR;
		pval->u.intv = 0;
	}
	// xxx else panic
}

// ----------------------------------------------------------------
mv_t s_ss_dot_func(mv_t* pval1, mv_t* pval2) {
	int len1 = strlen(pval1->u.strv);
	int len2 = strlen(pval1->u.strv);
	int len3 = len1 + len2 + 1; // for the null-terminator byte
	char* string3 = mlr_malloc_or_die(len3);
	strcpy(&string3[0], pval1->u.strv);
	strcpy(&string3[len1], pval2->u.strv);

	// xxx encapsulate this:
	free(pval1->u.strv);
	free(pval2->u.strv);
	pval1->u.strv = NULL;
	pval2->u.strv = NULL;

	mv_t rv = {.type = MT_STRING, .u.strv = string3};
	return rv;
}

// ----------------------------------------------------------------
mv_t s_sss_sub_func(mv_t* pval1, mv_t* pval2, mv_t* pval3) {
	char* substr = strstr(pval1->u.strv, pval2->u.strv);
	if (substr == NULL) {
		return *pval1;
	} else {
		int  len1 = substr - pval1->u.strv;
		int olen2 = strlen(pval2->u.strv);
		int nlen2 = strlen(pval3->u.strv);
		int  len3 = strlen(&pval1->u.strv[len1 + olen2]);
		int  len4 = len1 + nlen2 + len3;

		char* string4 = mlr_malloc_or_die(len4);
		strncpy(&string4[0],    pval1->u.strv, len1);
		strncpy(&string4[len1], pval3->u.strv, nlen2);
		strncpy(&string4[len1+nlen2], &pval1->u.strv[len1+olen2], len3);

		free(pval1->u.strv);
		free(pval2->u.strv);
		free(pval3->u.strv);
		pval1->u.strv = NULL;
		pval2->u.strv = NULL;
		pval3->u.strv = NULL;

		mv_t rv = {.type = MT_STRING, .u.strv = string4};
		return rv;
	}
}

// ----------------------------------------------------------------
// xxx cmt mem-mgt & contract. similar to lrec-mapper contract.
mv_t s_s_tolower_func(mv_t* pval1) {
	char* string = strdup(pval1->u.strv);
	for (char* c = string; *c; c++)
		*c = tolower(*c);
	// xxx encapsulate this:
	free(pval1->u.strv);
	pval1->u.strv = NULL;

	mv_t rv = {.type = MT_STRING, .u.strv = string};
	return rv;
}

// xxx cmt mem-mgt & contract. similar to lrec-mapper contract.
mv_t s_s_toupper_func(mv_t* pval1) {
	char* string = strdup(pval1->u.strv);
	for (char* c = string; *c; c++)
		*c = toupper(*c);
	// xxx encapsulate this:
	free(pval1->u.strv);
	pval1->u.strv = NULL;

	mv_t rv = {.type = MT_STRING, .u.strv = string};
	return rv;
}

// ----------------------------------------------------------------
mv_t s_f_sec2gmt_func(mv_t* pval1) {
	ERROR_OUT(*pval1);
	mt_get_double_nullable(pval1);
	NULL_OUT(*pval1);
	if (pval1->type != MT_DOUBLE)
		return MV_ERROR;
	time_t clock = (time_t) pval1->u.dblv;
	struct tm tm;
	struct tm *ptm = gmtime_r(&clock, &tm);
	// xxx use retval which is size_t
	// xxx error-check all of this ...
	char* string = mlr_malloc_or_die(32);
	(void)strftime(string, 32, "%Y-%m-%dT%H:%M:%SZ", ptm);

	mv_t rv = {.type = MT_STRING, .u.strv = string};
	return rv;
}

mv_t i_s_gmt2sec_func(mv_t* pval1) {
	struct tm tm;
	if (*pval1->u.strv == '\0') {
		return MV_NULL;
	} else {
		strptime(pval1->u.strv, "%Y-%m-%dT%H:%M:%SZ", &tm);
		time_t t = timegm(&tm);

		mv_t rv = {.type = MT_INT, .u.intv = (long long)t};
		return rv;
	}
}

// ----------------------------------------------------------------
mv_t i_s_strlen_func(mv_t* pval1) {
	mv_t rv = {.type = MT_INT, .u.intv = strlen(pval1->u.strv)};
	return rv;
}

// ----------------------------------------------------------------
static mv_t int_i_n(mv_t* pa) { return (mv_t) {.type = MT_NULL,  .u.intv = 0}; }
static mv_t int_i_e(mv_t* pa) { return (mv_t) {.type = MT_ERROR, .u.intv = 0}; }
static mv_t int_i_b(mv_t* pa) { return (mv_t) {.type = MT_INT,   .u.intv = pa->u.boolv ? 1 : 0}; }
static mv_t int_i_d(mv_t* pa) { return (mv_t) {.type = MT_INT,   .u.intv = (long long)round(pa->u.dblv)}; }
static mv_t int_i_i(mv_t* pa) { return (mv_t) {.type = MT_INT,   .u.intv = pa->u.intv}; }
static mv_t int_i_s(mv_t* pa) {
	mv_t retval = (mv_t) {.type = MT_INT };
	if (*pa->u.strv == '\0')
		return MV_NULL;
	if (!mlr_try_int_from_string(pa->u.strv, &retval.u.intv))
		retval.type = MT_ERROR;
	return retval;
}

static mv_unary_func_t* int_dispositions[MT_MAX] = {
    /*NULL*/   int_i_n,
    /*ERROR*/  int_i_e,
    /*BOOL*/   int_i_b,
    /*DOUBLE*/ int_i_d,
    /*INT*/    int_i_i,
    /*STRING*/ int_i_s,
};

mv_t i_x_int_func(mv_t* pval1) { return (int_dispositions[pval1->type])(pval1); }

// ----------------------------------------------------------------
// xxx i'm using double & long long but saying double & int. this is confusing & needs fixing.
static mv_t float_f_n(mv_t* pa) { return (mv_t) {.type = MT_NULL,   .u.intv = 0}; }
static mv_t float_f_e(mv_t* pa) { return (mv_t) {.type = MT_ERROR,  .u.intv = 0}; }
static mv_t float_f_b(mv_t* pa) { return (mv_t) {.type = MT_DOUBLE, .u.dblv = pa->u.boolv ? 1.0 : 0.0}; }
static mv_t float_f_d(mv_t* pa) { return (mv_t) {.type = MT_DOUBLE, .u.dblv = pa->u.dblv}; }
static mv_t float_f_i(mv_t* pa) { return (mv_t) {.type = MT_DOUBLE, .u.dblv = pa->u.intv}; }
static mv_t float_f_s(mv_t* pa) {
	mv_t retval = (mv_t) {.type = MT_DOUBLE };
	if (*pa->u.strv == '\0')
		return MV_NULL;
	if (!mlr_try_double_from_string(pa->u.strv, &retval.u.dblv))
		retval.type = MT_ERROR;
	return retval;
}

static mv_unary_func_t* float_dispositions[MT_MAX] = {
    /*NULL*/   float_f_n,
    /*ERROR*/  float_f_e,
    /*BOOL*/   float_f_b,
    /*DOUBLE*/ float_f_d,
    /*INT*/    float_f_i,
    /*STRING*/ float_f_s,
};

mv_t f_x_float_func(mv_t* pval1) { return (float_dispositions[pval1->type])(pval1); }

// ----------------------------------------------------------------
static mv_t boolean_b_n(mv_t* pa) { return (mv_t) {.type = MT_NULL,  .u.intv = 0}; }
static mv_t boolean_b_e(mv_t* pa) { return (mv_t) {.type = MT_ERROR, .u.intv = 0}; }
static mv_t boolean_b_b(mv_t* pa) { return (mv_t) {.type = MT_BOOL,  .u.boolv = pa->u.boolv}; }
static mv_t boolean_b_d(mv_t* pa) { return (mv_t) {.type = MT_BOOL,  .u.boolv = (pa->u.dblv == 0.0) ? FALSE : TRUE}; }
static mv_t boolean_b_i(mv_t* pa) { return (mv_t) {.type = MT_BOOL,  .u.boolv = (pa->u.intv == 0LL) ? FALSE : TRUE}; }
static mv_t boolean_b_s(mv_t* pa) { return (mv_t) {.type = MT_BOOL,
		.u.boolv = (streq(pa->u.strv, "true") || streq(pa->u.strv, "TRUE")) ? TRUE : FALSE
	};
}

static mv_unary_func_t* boolean_dispositions[MT_MAX] = {
    /*NULL*/   boolean_b_n,
    /*ERROR*/  boolean_b_e,
    /*BOOL*/   boolean_b_b,
    /*DOUBLE*/ boolean_b_d,
    /*INT*/    boolean_b_i,
    /*STRING*/ boolean_b_s,
};

mv_t b_x_boolean_func(mv_t* pval1) { return (boolean_dispositions[pval1->type])(pval1); }

// ----------------------------------------------------------------
static mv_t string_s_n(mv_t* pa) { return (mv_t) {.type = MT_NULL,   .u.intv = 0}; }
static mv_t string_s_e(mv_t* pa) { return (mv_t) {.type = MT_ERROR,  .u.intv = 0}; }
static mv_t string_s_b(mv_t* pa) { return (mv_t) {.type = MT_STRING, .u.strv = strdup(pa->u.boolv?"true":"false")}; }
static mv_t string_s_d(mv_t* pa) {
	return (mv_t) {.type = MT_STRING, .u.strv = mlr_alloc_string_from_double(pa->u.dblv, MLR_GLOBALS.ofmt)};
}
static mv_t string_s_i(mv_t* pa) { return (mv_t) {.type = MT_STRING, .u.strv = mlr_alloc_string_from_ll(pa->u.intv)}; }
static mv_t string_s_s(mv_t* pa) { return (mv_t) {.type = MT_STRING, .u.strv = pa->u.strv}; }

static mv_unary_func_t* string_dispositions[MT_MAX] = {
    /*NULL*/   string_s_n,
    /*ERROR*/  string_s_e,
    /*BOOL*/   string_s_b,
    /*DOUBLE*/ string_s_d,
    /*INT*/    string_s_i,
    /*STRING*/ string_s_s,
};

mv_t s_x_string_func(mv_t* pval1) { return (string_dispositions[pval1->type])(pval1); }

// ----------------------------------------------------------------
// xxx cmt us!!!!

static mv_t op_n_xx(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_NULL, .u.intv = 0}; }
static mv_t op_e_xx(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_ERROR, .u.intv = 0}; }

static  mv_t eq_b_ii(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv == pb->u.intv}; }
static  mv_t ne_b_ii(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv != pb->u.intv}; }
static  mv_t gt_b_ii(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv >  pb->u.intv}; }
static  mv_t ge_b_ii(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv >= pb->u.intv}; }
static  mv_t lt_b_ii(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv <  pb->u.intv}; }
static  mv_t le_b_ii(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv <= pb->u.intv}; }

static  mv_t eq_b_ff(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv == pb->u.dblv}; }
static  mv_t ne_b_ff(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv != pb->u.dblv}; }
static  mv_t gt_b_ff(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv >  pb->u.dblv}; }
static  mv_t ge_b_ff(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv >= pb->u.dblv}; }
static  mv_t lt_b_ff(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv <  pb->u.dblv}; }
static  mv_t le_b_ff(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv <= pb->u.dblv}; }

static  mv_t eq_b_fi(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv == pb->u.intv}; }
static  mv_t ne_b_fi(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv != pb->u.intv}; }
static  mv_t gt_b_fi(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv >  pb->u.intv}; }
static  mv_t ge_b_fi(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv >= pb->u.intv}; }
static  mv_t lt_b_fi(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv <  pb->u.intv}; }
static  mv_t le_b_fi(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.dblv <= pb->u.intv}; }

static  mv_t eq_b_if(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv == pb->u.dblv}; }
static  mv_t ne_b_if(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv != pb->u.dblv}; }
static  mv_t gt_b_if(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv >  pb->u.dblv}; }
static  mv_t ge_b_if(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv >= pb->u.dblv}; }
static  mv_t lt_b_if(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv <  pb->u.dblv}; }
static  mv_t le_b_if(mv_t* pa, mv_t* pb) { return (mv_t) {.type = MT_BOOL, .u.boolv = pa->u.intv <= pb->u.dblv}; }

static  mv_t eq_b_xs(mv_t* pa, mv_t* pb) {
	char* a = mt_format_val(pa);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(a, pb->u.strv) == 0};
	free(a);
	return rv;
}
static  mv_t ne_b_xs(mv_t* pa, mv_t* pb) {
	char* a = mt_format_val(pa);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(a, pb->u.strv) != 0};
	free(a);
	return rv;
}
static  mv_t gt_b_xs(mv_t* pa, mv_t* pb) {
	char* a = mt_format_val(pa);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(a, pb->u.strv) >  0};
	free(a);
	return rv;
}
static  mv_t ge_b_xs(mv_t* pa, mv_t* pb) {
	char* a = mt_format_val(pa);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(a, pb->u.strv) >= 0};
	free(a);
	return rv;
}
static  mv_t lt_b_xs(mv_t* pa, mv_t* pb) {
	char* a = mt_format_val(pa);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(a, pb->u.strv) <  0};
	free(a);
	return rv;
}
static  mv_t le_b_xs(mv_t* pa, mv_t* pb) {
	char* a = mt_format_val(pa);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(a, pb->u.strv) <= 0};
	free(a);
	return rv;
}

static  mv_t eq_b_sx(mv_t* pa, mv_t* pb) {
	char* b = mt_format_val(pb);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(pa->u.strv, b) == 0};
	free(b);
	return rv;
}
static  mv_t ne_b_sx(mv_t* pa, mv_t* pb) {
	char* b = mt_format_val(pb);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(pa->u.strv, b) != 0};
	free(b);
	return rv;
}
static  mv_t gt_b_sx(mv_t* pa, mv_t* pb) {
	char* b = mt_format_val(pb);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(pa->u.strv, b) >  0};
	free(b);
	return rv;
}
static  mv_t ge_b_sx(mv_t* pa, mv_t* pb) {
	char* b = mt_format_val(pb);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(pa->u.strv, b) >= 0};
	free(b);
	return rv;
}
static  mv_t lt_b_sx(mv_t* pa, mv_t* pb) {
	char* b = mt_format_val(pb);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(pa->u.strv, b) <  0};
	free(b);
	return rv;
}
static  mv_t le_b_sx(mv_t* pa, mv_t* pb) {
	char* b = mt_format_val(pb);
	mv_t rv = {.type = MT_BOOL, .u.boolv = strcmp(pa->u.strv, b) <= 0};
	free(b);
	return rv;
}

static mv_t eq_b_ss(mv_t*pa, mv_t*pb) {return (mv_t){.type=MT_BOOL, .u.boolv=strcmp(pa->u.strv, pb->u.strv) == 0};}
static mv_t ne_b_ss(mv_t*pa, mv_t*pb) {return (mv_t){.type=MT_BOOL, .u.boolv=strcmp(pa->u.strv, pb->u.strv) != 0};}
static mv_t gt_b_ss(mv_t*pa, mv_t*pb) {return (mv_t){.type=MT_BOOL, .u.boolv=strcmp(pa->u.strv, pb->u.strv) >  0};}
static mv_t ge_b_ss(mv_t*pa, mv_t*pb) {return (mv_t){.type=MT_BOOL, .u.boolv=strcmp(pa->u.strv, pb->u.strv) >= 0};}
static mv_t lt_b_ss(mv_t*pa, mv_t*pb) {return (mv_t){.type=MT_BOOL, .u.boolv=strcmp(pa->u.strv, pb->u.strv) <  0};}
static mv_t le_b_ss(mv_t*pa, mv_t*pb) {return (mv_t){.type=MT_BOOL, .u.boolv=strcmp(pa->u.strv, pb->u.strv) <= 0};}

static mv_binary_func_t* eq_dispositions[MT_MAX][MT_MAX] = {
    //         NULL      ERROR    BOOL     DOUBLE   INT      STRING
    /*NULL*/   {op_n_xx, op_e_xx, op_e_xx, op_n_xx, op_n_xx, op_n_xx},
    /*ERROR*/  {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*BOOL*/   {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*DOUBLE*/ {op_n_xx, op_e_xx, op_e_xx, eq_b_ff, eq_b_fi, eq_b_xs},
    /*INT*/    {op_n_xx, op_e_xx, op_e_xx, eq_b_if, eq_b_ii, eq_b_xs},
    /*STRING*/ {op_n_xx, op_e_xx, op_e_xx, eq_b_sx, eq_b_sx, eq_b_ss},
};

static mv_binary_func_t* ne_dispositions[MT_MAX][MT_MAX] = {
    //         NULL      ERROR    BOOL     DOUBLE   INT      STRING
    /*NULL*/   {op_n_xx, op_e_xx, op_e_xx, op_n_xx, op_n_xx, op_n_xx},
    /*ERROR*/  {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*BOOL*/   {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*DOUBLE*/ {op_n_xx, op_e_xx, op_e_xx, ne_b_ff, ne_b_fi, ne_b_xs},
    /*INT*/    {op_n_xx, op_e_xx, op_e_xx, ne_b_if, ne_b_ii, ne_b_xs},
    /*STRING*/ {op_n_xx, op_e_xx, op_e_xx, ne_b_sx, ne_b_sx, ne_b_ss},
};

static mv_binary_func_t* gt_dispositions[MT_MAX][MT_MAX] = {
    //         NULL      ERROR    BOOL     DOUBLE   INT      STRING
    /*NULL*/   {op_n_xx, op_e_xx, op_e_xx, op_n_xx, op_n_xx, op_n_xx},
    /*ERROR*/  {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*BOOL*/   {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*DOUBLE*/ {op_n_xx, op_e_xx, op_e_xx, gt_b_ff, gt_b_fi, gt_b_xs},
    /*INT*/    {op_n_xx, op_e_xx, op_e_xx, gt_b_if, gt_b_ii, gt_b_xs},
    /*STRING*/ {op_n_xx, op_e_xx, op_e_xx, gt_b_sx, gt_b_sx, gt_b_ss},
};

static mv_binary_func_t* ge_dispositions[MT_MAX][MT_MAX] = {
    //         NULL      ERROR    BOOL     DOUBLE   INT      STRING
    /*NULL*/   {op_n_xx, op_e_xx, op_e_xx, op_n_xx, op_n_xx, op_n_xx},
    /*ERROR*/  {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*BOOL*/   {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*DOUBLE*/ {op_n_xx, op_e_xx, op_e_xx, ge_b_ff, ge_b_fi, ge_b_xs},
    /*INT*/    {op_n_xx, op_e_xx, op_e_xx, ge_b_if, ge_b_ii, ge_b_xs},
    /*STRING*/ {op_n_xx, op_e_xx, op_e_xx, ge_b_sx, ge_b_sx, ge_b_ss},
};

static mv_binary_func_t* lt_dispositions[MT_MAX][MT_MAX] = {
    //         NULL      ERROR    BOOL     DOUBLE   INT      STRING
    /*NULL*/   {op_n_xx, op_e_xx, op_e_xx, op_n_xx, op_n_xx, op_n_xx},
    /*ERROR*/  {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*BOOL*/   {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*DOUBLE*/ {op_n_xx, op_e_xx, op_e_xx, lt_b_ff, lt_b_fi, lt_b_xs},
    /*INT*/    {op_n_xx, op_e_xx, op_e_xx, lt_b_if, lt_b_ii, lt_b_xs},
    /*STRING*/ {op_n_xx, op_e_xx, op_e_xx, lt_b_sx, lt_b_sx, lt_b_ss},
};

static mv_binary_func_t* le_dispositions[MT_MAX][MT_MAX] = {
    //         NULL      ERROR    BOOL     DOUBLE   INT      STRING
    /*NULL*/   {op_n_xx, op_e_xx, op_e_xx, op_n_xx, op_n_xx, op_n_xx},
    /*ERROR*/  {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*BOOL*/   {op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx, op_e_xx},
    /*DOUBLE*/ {op_n_xx, op_e_xx, op_e_xx, le_b_ff, le_b_fi, le_b_xs},
    /*INT*/    {op_n_xx, op_e_xx, op_e_xx, le_b_if, le_b_ii, le_b_xs},
    /*STRING*/ {op_n_xx, op_e_xx, op_e_xx, le_b_sx, le_b_sx, le_b_ss},
};

mv_t eq_op_func(mv_t* pval1, mv_t* pval2) { return (eq_dispositions[pval1->type][pval2->type])(pval1, pval2); }
mv_t ne_op_func(mv_t* pval1, mv_t* pval2) { return (ne_dispositions[pval1->type][pval2->type])(pval1, pval2); }
mv_t gt_op_func(mv_t* pval1, mv_t* pval2) { return (gt_dispositions[pval1->type][pval2->type])(pval1, pval2); }
mv_t ge_op_func(mv_t* pval1, mv_t* pval2) { return (ge_dispositions[pval1->type][pval2->type])(pval1, pval2); }
mv_t lt_op_func(mv_t* pval1, mv_t* pval2) { return (lt_dispositions[pval1->type][pval2->type])(pval1, pval2); }
mv_t le_op_func(mv_t* pval1, mv_t* pval2) { return (le_dispositions[pval1->type][pval2->type])(pval1, pval2); }
