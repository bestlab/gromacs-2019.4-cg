/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2012,2014,2015,2017,2018, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
/* This file is completely threadsafe - keep it that way! */
#include "gmxpre.h"

#include "ebin.h"

#include <cmath>
#include <cstring>

#include "gromacs/math/units.h"
#include "gromacs/math/utilities.h"
#include "gromacs/math/vec.h"
#include "gromacs/topology/ifunc.h"
#include "gromacs/trajectory/energyframe.h"
#include "gromacs/utility/cstringutil.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/smalloc.h"

t_ebin *mk_ebin()
{
    t_ebin *eb;

    snew(eb, 1);

    return eb;
}

void done_ebin(t_ebin *eb)
{
    for (int i = 0; i < eb->nener; i++)
    {
        sfree(eb->enm[i].name);
        sfree(eb->enm[i].unit);
    }
    sfree(eb->e);
    sfree(eb->e_sim);
    sfree(eb->enm);
}

int get_ebin_space(t_ebin *eb, int nener, const char *const enm[], const char *unit)
{
    int         index;
    int         i, f;
    const char *u;

    index      = eb->nener;
    eb->nener += nener;
    srenew(eb->e, eb->nener);
    srenew(eb->e_sim, eb->nener);
    srenew(eb->enm, eb->nener);
    for (i = index; (i < eb->nener); i++)
    {
        eb->e[i].e        = 0;
        eb->e[i].eav      = 0;
        eb->e[i].esum     = 0;
        eb->e_sim[i].e    = 0;
        eb->e_sim[i].eav  = 0;
        eb->e_sim[i].esum = 0;
        eb->enm[i].name   = gmx_strdup(enm[i-index]);
        if (unit != nullptr)
        {
            eb->enm[i].unit = gmx_strdup(unit);
        }
        else
        {
            /* Determine the unit from the longname.
             * These units should have been defined in ifunc.c
             * But even better would be if all interactions functions
             * return energies and all non-interaction function
             * entries would be removed from the ifunc array.
             */
            u = unit_energy;
            for (f = 0; f < F_NRE; f++)
            {
                if (strcmp(eb->enm[i].name,
                           interaction_function[f].longname) == 0)
                {
                    /* Only the terms in this list are not energies */
                    switch (f)
                    {
                        case F_DISRESVIOL: u = unit_length;   break;
                        case F_ORIRESDEV:  u = "obs";         break;
                        case F_TEMP:       u = unit_temp_K;   break;
                        case F_PDISPCORR:
                        case F_PRES:       u = unit_pres_bar; break;
                    }
                }
            }
            eb->enm[i].unit = gmx_strdup(u);
        }
    }

    return index;
}

void add_ebin(t_ebin *eb, int index, int nener, const real ener[], gmx_bool bSum)
{
    int       i, m;
    double    e, invmm, diff;
    t_energy *eg, *egs;

    if ((index+nener > eb->nener) || (index < 0))
    {
        gmx_fatal(FARGS, "%s-%d: Energies out of range: index=%d nener=%d maxener=%d",
                  __FILE__, __LINE__, index, nener, eb->nener);
    }

    eg = &(eb->e[index]);

    for (i = 0; (i < nener); i++)
    {
        eg[i].e      = ener[i];
    }

    if (bSum)
    {
        egs = &(eb->e_sim[index]);

        m = eb->nsum;

        if (m == 0)
        {
            for (i = 0; (i < nener); i++)
            {
                eg[i].eav    = 0;
                eg[i].esum   = ener[i];
                egs[i].esum += ener[i];
            }
        }
        else
        {
            invmm = (1.0/m)/(m+1.0);

            for (i = 0; (i < nener); i++)
            {
                /* Value for this component */
                e = ener[i];

                /* first update sigma, then sum */
                diff         = eg[i].esum - m*e;
                eg[i].eav   += diff*diff*invmm;
                eg[i].esum  += e;
                egs[i].esum += e;
            }
        }
    }
}

void ebin_increase_count(t_ebin *eb, gmx_bool bSum)
{
    eb->nsteps++;
    eb->nsteps_sim++;

    if (bSum)
    {
        eb->nsum++;
        eb->nsum_sim++;
    }
}

void reset_ebin_sums(t_ebin *eb)
{
    eb->nsteps = 0;
    eb->nsum   = 0;
    /* The actual sums are cleared when the next frame is stored */
}

void pr_ebin(FILE *fp, t_ebin *eb, int index, int nener, int nperline,
             int prmode, gmx_bool bPrHead)
{
    int  i, j, i0;
    int  rc;
    char buf[30];

    rc = 0;

    if (index < 0 || index > eb->nener)
    {
        gmx_fatal(FARGS, "Invalid index in pr_ebin: %d", index);
    }
    int start = index;
    if (nener > eb->nener)
    {
        gmx_fatal(FARGS, "Invalid nener in pr_ebin: %d", nener);
    }
    int end = eb->nener;
    if (nener != -1)
    {
        end = index + nener;
    }
    for (i = start; (i < end) && rc >= 0; )
    {
        if (bPrHead)
        {
            i0 = i;
            for (j = 0; (j < nperline) && (i < end) && rc >= 0; j++, i++)
            {
                if (strncmp(eb->enm[i].name, "Pres", 4) == 0)
                {
                    /* Print the pressure unit to avoid confusion */
                    sprintf(buf, "%s (%s)", eb->enm[i].name, unit_pres_bar);
                    rc = fprintf(fp, "%15s", buf);
                }
                else
                {
                    rc = fprintf(fp, "%15s", eb->enm[i].name);
                }
            }

            if (rc >= 0)
            {
                rc = fprintf(fp, "\n");
            }

            i = i0;
        }
        for (j = 0; (j < nperline) && (i < end) && rc >= 0; j++, i++)
        {
            switch (prmode)
            {
                case eprNORMAL:
                    rc = fprintf(fp, "   %12.5e", eb->e[i].e);
                    break;
                case eprAVER:
                    if (eb->nsum_sim > 0)
                    {
                        rc = fprintf(fp, "   %12.5e", eb->e_sim[i].esum/eb->nsum_sim);
                    }
                    else
                    {
                        rc = fprintf(fp, "    %-12s", "N/A");
                    }
                    break;
                default: gmx_fatal(FARGS, "Invalid print mode %d in pr_ebin",
                                   prmode);
            }

        }
        if (rc >= 0)
        {
            rc = fprintf(fp, "\n");
        }
    }
    if (rc < 0)
    {
        gmx_fatal(FARGS, "Cannot write to logfile; maybe you are out of disk space?");
    }
}
