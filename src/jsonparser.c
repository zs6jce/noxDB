/* SYSIFCOPT(*IFSIO) TERASPACE(*YES *TSIFC) STGMDL(*SNGLVL) */
/* ------------------------------------------------------------- *
 * Company . . . : System & Method A/S                           *
 * Design  . . . : Niels Liisberg                                *
 * Function  . . : JSON Parser                                   *
 *                                                               *
 * By     Date     Task    Description                           *
 * NL     02.06.03 0000000 New program                           *
 * NL     27.02.08 0000510 Allow also no namespace for *:tag     *
 * NL     27.02.08 0000510 jx_NodeCopy                           *
 * NL     13.05.08 0000577 jx_NodeAdd / WriteNote                *
 * NL     13.05.08 0000577 Support for refference location       *
 * ------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <leod.h>
#include <decimal.h>
#include <wchar.h>
// #include <errno.h>

#include <sys/stat.h>
#include "ostypes.h"
#include "varchar.h"
#include "xlate.h"
#include "parms.h"
#include "rtvsysval.h"
#include "mem001.h"
#include "noxdb.h"




/* -------------------------------------------------------------*/
void jx_DecodeJsonBuf2Str (  PUCHAR v, PUCHAR t , LONG len)
{
	#pragma convert(1252)
	int i;
	PUCHAR end;
	USHORT wc;
	UCHAR c;

	for ( end = t + len ; t < end ; t++) {
		if (*t == BACKSLASH) {
			UCHAR n;
		  	n  = *( ++t);
		  	switch (n) {
			case 'u':  // UNICODE
				for (wc=i=0;i<4; i++) {
					wc  = wc  * 16 + parsehex(*++t);
				}
				v += unicode2utf8(v, wc);
				break;
	  		// Note!! never store special types in the object-graph ....
			case  'n': // new line
				*v++ = '\n' ;
				break;
			case 'r' : // return
				*v++ = '\r' ;
				break;
			case 't':  // tab
				*v++ = '\t' ;
				break;
			case '"':  // Quotes
				*v++ = '"' ;
				break;
			default:
				if (n != BACKSLASH) {
					*v++ = BACKSLASH;
				}
				*v++ = n;
				break;
			}
		} else {
		  	*v++ = *t;
		}
	}
	*v = '\0';
	#pragma convert(0)
}
/* -------------------------------------------------------------*/
void jx_DecodeJson (  PUCHAR v )
{
	PUCHAR temp  = memStrDup(v);
	jx_DecodeJsonBuf2Str (v, temp , strlen (temp));
	memFree(&temp);
}
//---------------------------------------------------------------------------
BOOL isTermChar(UCHAR c)
{
	if (c == COMMA
	||  c == COLON
	||  c == BRAEND
	||  c == CUREND
	||  c == BRABEG
	||  c == CURBEG) {
		return TRUE;
	} else {
		return FALSE;
	}
}
//---------------------------------------------------------------------------
TOK getTok(PJXCOM pJxCom) 
{

	#pragma convert(1252)

	PUCHAR start;
	PUCHAR p;
	int skip;
	TOK tok;

	memset (&tok ,0, sizeof(tok));

	pJxCom->tokenNo ++;

	// Debugger token number
	/*
	if ( pJxCom->tokenNo  == 5) {
		int debugger = 1;
	}
	*/

	for(;;) {
		start = jx_GetChar(pJxCom);
		if (pJxCom->State == XML_EXIT || pJxCom->State == XML_EXIT_ERROR) {
			tok.isEof = TRUE;
			return tok;
		}
		if (*start > ' ') break; // FOUND!!
	}

	if (*start == QUOT  || *start == APOS ) {
		UCHAR fnyt = *start;
		start++; // skip the "

		for  ( p = start ;; p++) {     // Find length
			if (*p == '\0') break;
			if (*p == fnyt) {            // we have a candidate "
				// now if we have an even number of \ it is actually escaped backslash so we continue
				PUCHAR esc;
				BOOL   flipflop = true;
				for (esc = p-1; *(esc--) == BACKSLASH ; flipflop = true - flipflop);
				if (flipflop) break;
			}
		}

		tok.len = p - start;
		skip = tok.len + 1;
		tok.data  = (PUCHAR) memAlloc ( tok.len  + 1);        // Copy to heap and keep space for terminating zero
		jx_DecodeJsonBuf2Str ( tok.data, start , tok.len);
	} else if  (isTermChar(*start)) {
		tok.token = *start;
		tok.len   = 1;
		skip      = 0;
	} else {
		for  ( p = start; *p > ' ' && ! isTermChar(*p) ; p++);    // Find length
		tok.len = p - start;
		tok.data  = (PUCHAR) memAlloc ( tok.len  + 1);        // Copy to heap and keep space for terminating zero
		substr (tok.data, start , tok.len);
		skip = tok.len - 1;
		tok.isLiteral = TRUE;
	}


	/* DEBUG!!
	printf("\n%d <%s>",  dbgStep , out);
	printf("\n[%-0.130s]", start);


	if (strstr(start , "Division,descript")) {
		printf("\n!!!!%d ",  dbgStep);
	}
	*/

	jx_SkipChars ( pJxCom , skip );
	// for  (;  *p <= ' ' && *p ; p++);                // Skip until next token
	// *s = p;
	return tok;
	#pragma convert(0)

}
//---------------------------------------------------------------------------
PJXNODE jx_AppendType (PJXCOM pJxCom , PJXNODE pCurNode , JSTATE type , PUCHAR name, JSTATE parentType)
{
	PJXNODE pNewNode;
	if ( parentType == ROOT) { // Handle root
		pNewNode = pCurNode;
		pNewNode->type = type;
		pNewNode->signature  = NODESIG;
	} else {
		pNewNode = jx_NodeAdd (pCurNode , RL_LAST_CHILD, name  , NULL, type);
	}
	jx_ParseJsonNode (pJxCom, type , name, pNewNode);
	return pNewNode;
}
//---------------------------------------------------------------------------
void jx_setRootNode( PJXNODE pNode, TOK t)
{

	PUCHAR value = (BeginsWith(t.data, NULLSTR) && t.isLiteral) ? NULL : t.data;
	jx_NodeSet (pNode , value);
	pNode->isLiteral = t.isLiteral;
}
//---------------------------------------------------------------------------
BOOL jx_ParseJsonNode(PJXCOM pJxCom, JSTATE state,  PUCHAR name , PJXNODE pCurNode )
{

	TOK t;


	if (pJxCom->State == XML_EXIT_ERROR) return TRUE;
	if (pCurNode->lineNo == 0) pCurNode->lineNo = pJxCom->LineCount;

	switch(state) {
		case VALUE:
		case ROOT : {
			PJXNODE pNewNode;
			t = getTok(pJxCom);
			if (t.isEof) {
				return FALSE;
			} else if (t.token  == CURBEG) {
				pNewNode = jx_AppendType ( pJxCom, pCurNode ,  OBJECT , name, state);
			} else if (t.token == BRABEG) {
				pNewNode = jx_AppendType ( pJxCom, pCurNode ,  ARRAY  , name, state );
			} else if (t.token == BRAEND && pCurNode->type == ARRAY) {  // Empty array
				return TRUE;
			} else if (t.data == NULL) {
				jx_SetMessage( "Invalid token %s or no data when parsing an object at (%d:%d) token number: %d"
											,c2s(t.token), pJxCom->LineCount, pJxCom->ColCount, pJxCom->tokenNo);
				pJxCom->State = XML_EXIT_ERROR;
				return TRUE;
			} else if (state == ROOT) {
				jx_setRootNode( pCurNode, t);
				return FALSE;
			} else if (BeginsWith(t.data, NULLSTR) && t.isLiteral) {
				pNewNode = jx_NodeAdd (pCurNode, RL_LAST_CHILD, name , NULL , VALUE);
			} else {
				pNewNode = jx_NodeAdd (pCurNode, RL_LAST_CHILD, name , t.data , VALUE);
				pNewNode->isLiteral = t.isLiteral;
			}
			memFree (&t.data);
			if (pNewNode->lineNo == 0) pNewNode->lineNo = pJxCom->LineCount;
				return FALSE;
			}

		case OBJECT:
			for(;;) {
				TOK  key, sep;
				key = getTok(pJxCom);
				if (key.isEof) {
					jx_SetMessage( "Invalid end of stream when parsing an object at (%d:%d) token number: %d"
												, pJxCom->LineCount, pJxCom->ColCount, pJxCom->tokenNo);
					pJxCom->State = XML_EXIT_ERROR;
					return TRUE;
				}

				// Needed for empty object and if extra, end of attribute list
				if (key.token == CUREND) {  // the }
					return FALSE;
				}
				sep = getTok(pJxCom);
				if (sep.isEof) {
					jx_SetMessage( "Invalid end of stream at (%d:%d) token number: %d. Was expecting an :"
												, pJxCom->LineCount, pJxCom->ColCount, pJxCom->tokenNo);
					pJxCom->State = XML_EXIT_ERROR;
					memFree (&key.data);
					return TRUE;
				}
				if (sep.token != COLON)  {
					jx_SetMessage( "Invalid token at (%d:%d) token number: %d. Was expecting an ':' but got a %s near %s"
												, pJxCom->LineCount, pJxCom->ColCount, pJxCom->tokenNo , c2s(sep.token), sep.data );
					pJxCom->State = XML_EXIT_ERROR;
					memFree (&key.data);
					memFree (&sep.data);
					return TRUE;
				}

				memFree (&sep.data);
				jx_ParseJsonNode (pJxCom, VALUE, key.data , pCurNode);
				memFree (&key.data);

				sep = getTok(pJxCom);
				if (sep.isEof) {
					jx_SetMessage( "Invalid end of stream at (%d:%d) token number: %d. Was expecting an sepeator or end of object"
												, pJxCom->LineCount, pJxCom->ColCount, pJxCom->tokenNo);
					pJxCom->State = XML_EXIT_ERROR;
					return TRUE;
				}
				if (sep.token == CUREND) { // The }
					memFree (&sep.data);
					return FALSE;
				}
				if (sep.token != COMMA)  {
					jx_SetMessage( "Invalid token at (%d:%d) token number: %d. Was expecting an ',' but got a %s near %s"
												, pJxCom->LineCount, pJxCom->ColCount, pJxCom->tokenNo, c2s(sep.token), sep.data );
					pJxCom->State = XML_EXIT_ERROR;
					memFree (&sep.data);
					return TRUE;
				}
				memFree (&sep.data);
			}

		case ARRAY: {
			TOK key;
			BOOL err;
			pCurNode->Count = 0;
			for (;;) {
				err = jx_ParseJsonNode(pJxCom, VALUE, NULL , pCurNode);
				if (err) return FALSE; // Empty array and not a real error

				// pCurNode->Count++; // Now done from addTal since counter can be updated by adding Nodeents from elsewhere

				key = getTok(pJxCom);
				if (key.isEof) {
					jx_SetMessage( "Invalid end of stream when parsing an array at (%d:%d) token number: %d "
												, pJxCom->LineCount, pJxCom->ColCount,pJxCom->tokenNo);
					pJxCom->State = XML_EXIT_ERROR;
					return TRUE;
				}
				if (key.token == BRAEND) {
					memFree (&key.data);
					return FALSE;
				}
				if (key.token != COMMA)  {
					jx_SetMessage( "Invalid token at (%d:%d) token number: %d. Was expecting an ',' but got a %s near %s"
												, pJxCom->LineCount, pJxCom->ColCount, pJxCom->tokenNo, c2s(key.token), a2eStr(key.data) );
					pJxCom->State = XML_EXIT_ERROR;
					memFree (&key.data);
					return TRUE;
				}
				memFree (&key.data);
			}
		}
	}
}
//---------------------------------------------------------------------------
BOOL jx_ParseJson(PJXCOM pJxCom)
{
	CheckBufSize(pJxCom);
	pJxCom->State = XML_FIND_START_TOKEN;
	jx_ParseJsonNode (pJxCom , ROOT, NULL , pJxCom->pNodeRoot );

	if (pJxCom->UseIconv) iconv_close(pJxCom->Iconv );
	memFree(&pJxCom->Data);
	fcloseAndSetNull(&pJxCom->File);

	return (pJxCom->State == XML_EXIT_ERROR);

}

