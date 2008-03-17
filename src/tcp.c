/* This implements the relp mapping onto TCP.
 *
 * Copyright 2008 by Rainer Gerhards and Adiscon GmbH.
 *
 * This file is part of librelp.
 *
 * Librelp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Librelp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Librelp.  If not, see <http://www.gnu.org/licenses/>.
 *
 * A copy of the GPL can be found in the file "COPYING" in this distribution.
 *
 * If the terms of the GPL are unsuitable for your needs, you may obtain
 * a commercial license from Adiscon. Contact sales@adiscon.com for further
 * details.
 *
 * ALL CONTRIBUTORS PLEASE NOTE that by sending contributions, you assign
 * your copyright to Adiscon GmbH, Germany. This is necessary to permit the
 * dual-licensing set forth here. Our apologies for this inconvenience, but
 * we sincerely believe that the dual-licensing model helps us provide great
 * free software while at the same time obtaining some funding for further
 * development.
 */
#include "config.h"
#include <stdlib.h>
#include "relp.h"
#include "tcp.h"


/** Construct a RELP tcp instance
 * This is the first thing that a caller must do before calling any
 * RELP function. The relp tcp must only destructed after all RELP
 * operations have been finished.
 */
relpRetVal
relpTcpConstruct(relpTcp_t **ppThis, relpEngine_t *pEngine)
{
	relpTcp_t *pThis;

	ENTER_RELPFUNC;
	assert(ppThis != NULL);
	if((pThis = calloc(1, sizeof(relpTcp_t))) == NULL) {
		ABORT_FINALIZE(RELP_RET_OUT_OF_MEMORY);
	}

	RELP_CORE_CONSTRUCTOR(pThis, Tcp);
	pThis->pEngine = pEngine;

	*ppThis = pThis;

finalize_it:
	LEAVE_RELPFUNC;
}


/** Destruct a RELP tcp instance
 */
relpRetVal
relpTcpDestruct(relpTcp_t **ppThis)
{
	relpTcp_t *pThis;
	int i;

	ENTER_RELPFUNC;
	assert(ppThis != NULL);
	pThis = *ppThis;
	RELPOBJ_assert(pThis, Tcp);

	/* TODO: check for pending operations -- rgerhards, 2008-03-16 */

	if(pThis->socks != NULL) {
		/* if we have some sockets at this stage, we need to close them */
		for(i = 1 ; i <= pThis->socks[0] ; ++i)
			close(pThis->socks[i]);
		free(pThis->socks);
	}
	/* done with de-init work, now free tcp object itself */
	free(pThis);
	*ppThis = NULL;

finalize_it:
	LEAVE_RELPFUNC;
}


/* initialize the tcp socket for a listner
 * pLstnPort is either a pointer to a port name or NULL, in which case the
 * default is used.
 * gerhards, 2008-03-17
 */
relpRetVal
relpTcpLstnInit(relpTcp_t *pThis, unsigned char *pLstnPort)
{
        struct addrinfo hints, *res, *r;
        int error, maxs, *s, on = 1;
	unsigned char *pLstnPt;

	ENTER_RELPFUNC;
	RELPOBJ_assert(pThis, Tcp);

	pLstnPt = (pLstnPort == NULL) ? RELP_DFTL_PORT : pLstnPort;
	dbgprintf("creating relp tcp listen socket on port %s\n", pLstnPt);

        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = family;
        hints.ai_socktype = SOCK_STREAM;

        error = getaddrinfo(NULL, pLstnPt, &hints, &res);
        if(error) {
		pThis->pEngine->dbgprint("error %d querying port '%s'\n", error, pLstnPt);
		ABORT_FINALIZE(RELP_RET_INVALID_PORT);
	}

        /* Count max number of sockets we may open */
        for(maxs = 0, r = res; r != NULL ; r = r->ai_next, maxs++)
		/* EMPTY */;
        pThis->socks = malloc((maxs+1) * sizeof(int));
        if (pThis->socks == NULL) {
               pThis->pEngine->dbgprint("couldn't allocate memory for TCP listen sockets, suspending RELP message reception.");
               freeaddrinfo(res);
               ABORT_FINALIZE(RELP_RET_OUT_OF_MEMORY);
        }

        *pThis->socks = 0;   /* num of sockets counter at start of array */
        s = pThis->socks + 1;
	for(r = res; r != NULL ; r = r->ai_next) {
               *s = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        	if (*s < 0) {
			if(!(r->ai_family == PF_INET6 && errno == EAFNOSUPPORT))
				pThis->pEngine->dbgprint("creating relp tcp listen socket");
				/* it is debatable if PF_INET with EAFNOSUPPORT should
				 * also be ignored...
				 */
                        continue;
                }

#ifdef IPV6_V6ONLY
                if (r->ai_family == AF_INET6) {
                	int iOn = 1;
			if (setsockopt(*s, IPPROTO_IPV6, IPV6_V6ONLY,
			      (char *)&iOn, sizeof (iOn)) < 0) {
			close(*s);
			*s = -1;
			continue;
                	}
                }
#endif
       		if (setsockopt(*s, SOL_SOCKET, SO_REUSEADDR,
			       (char *) &on, sizeof(on)) < 0 ) {
			errmsg.LogError(NO_ERRCODE, "TCP setsockopt(REUSEADDR)");
                        close(*s);
			*s = -1;
			continue;
		}

#if 0 // Do we really (still) need this?

		/* We need to enable BSD compatibility. Otherwise an attacker
		 * could flood our log files by sending us tons of ICMP errors.
		 */
#ifndef BSD	
		if(net.should_use_so_bsdcompat()) {
			if (setsockopt(*s, SOL_SOCKET, SO_BSDCOMPAT,
					(char *) &on, sizeof(on)) < 0) {
				errmsg.LogError(NO_ERRCODE, "TCP setsockopt(BSDCOMPAT)");
                                close(*s);
				*s = -1;
				continue;
			}
		}
#endif
#endif // #if 0

	        if( (bind(*s, r->ai_addr, r->ai_addrlen) < 0)
#ifndef IPV6_V6ONLY
		     && (errno != EADDRINUSE)
#endif
	           ) {
                        pThis->pEngine->dbgprint("error %d while binding relp tcp socket", errno);
                	close(*s);
			*s = -1;
                        continue;
                }

		if(listen(*s,pThis->iSessMax / 10 + 5) < 0) {
			/* If the listen fails, it most probably fails because we ask
			 * for a too-large backlog. So in this case we first set back
			 * to a fixed, reasonable, limit that should work. Only if
			 * that fails, too, we give up.
			 */
			pThis->pEngine->dbgprint("listen with a backlog of %d failed - retrying with default of 32.",
				    pThis->iSessMax / 10 + 5);
			if(listen(*s, 32) < 0) {
				errmsg.LogError(NO_ERRCODE, "TCP listen, suspending tcp inet");
	                	close(*s);
				*s = -1;
               		        continue;
			}
		}

		(*pThis->socks)++;
		s++;
	}

        if(res != NULL)
               freeaddrinfo(res);

	if(*pThis->socks != maxs)
		dbgprintf("We could initialize %d RELP TCP listen sockets out of %d we received "
		 	"- this may or may not be an error indication.\n", *pThis->socks, maxs);

        if(*pThis->socks == 0) {
		errmsg.LogError(NO_ERRCODE, "No RELP TCP listen socket could successfully be initialized, "
			 "message reception via RELP disabled.\n");
        	free(pThis->socks);
		ABORT_FINALIZE(RELP_RET_COULD_NOT_BIND);
	}

	LEAVE_RELPFUNC;
}
