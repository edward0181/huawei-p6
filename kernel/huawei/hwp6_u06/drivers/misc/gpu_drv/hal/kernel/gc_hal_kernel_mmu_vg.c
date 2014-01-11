/****************************************************************************
*
*    Copyright (c) 2005 - 2012 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************/




#include "gc_hal_kernel_precomp.h"

#if gcdENABLE_VG

#define _GC_OBJ_ZONE    gcvZONE_MMU

/*******************************************************************************
**
**  gckVGMMU_Construct
**
**  Construct a new gckVGMMU object.
**
**  INPUT:
**
**      gckVGKERNEL Kernel
**          Pointer to an gckVGKERNEL object.
**
**      gctSIZE_T MmuSize
**          Number of bytes for the page table.
**
**  OUTPUT:
**
**      gckVGMMU * Mmu
**          Pointer to a variable that receives the gckVGMMU object pointer.
*/
gceSTATUS gckVGMMU_Construct(
    IN gckVGKERNEL Kernel,
    IN gctSIZE_T MmuSize,
    OUT gckVGMMU * Mmu
    )
{
    gckOS os;
    gckVGHARDWARE hardware;
    gceSTATUS status;
    gckVGMMU mmu;
    gctUINT32 * pageTable;
    gctUINT32 i;

    gcmkHEADER_ARG("Kernel=0x%x MmuSize=0x%x Mmu=0x%x", Kernel, MmuSize, Mmu);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Kernel, gcvOBJ_KERNEL);
    gcmkVERIFY_ARGUMENT(MmuSize > 0);
    gcmkVERIFY_ARGUMENT(Mmu != gcvNULL);

    /* Extract the gckOS object pointer. */
    os = Kernel->os;
    gcmkVERIFY_OBJECT(os, gcvOBJ_OS);

    /* Extract the gckVGHARDWARE object pointer. */
    hardware = Kernel->hardware;
    gcmkVERIFY_OBJECT(hardware, gcvOBJ_HARDWARE);

    /* Allocate memory for the gckVGMMU object. */
    status = gckOS_Allocate(os, sizeof(struct _gckVGMMU), (gctPOINTER *) &mmu);

    if (status < 0)
    {
        /* Error. */
        gcmkFATAL(
            "%s(%d): could not allocate gckVGMMU object.",
            __FUNCTION__, __LINE__
            );

        gcmkFOOTER();
        return status;
    }

    /* Initialize the gckVGMMU object. */
    mmu->object.type = gcvOBJ_MMU;
    mmu->os = os;
    mmu->hardware = hardware;

    /* Create the mutex. */
    status = gckOS_CreateMutex(os, &mmu->mutex);

    if (status < 0)
    {
        /* Roll back. */
        mmu->object.type = gcvOBJ_UNKNOWN;
        gcmkVERIFY_OK(gckOS_Free(os, mmu));

        gcmkFOOTER();
        /* Error. */
        return status;
    }

    /* Allocate the page table. */
    mmu->pageTableSize = MmuSize;
    status = gckOS_AllocateContiguous(os,
                                      gcvFALSE,
                                      &mmu->pageTableSize,
                                      &mmu->pageTablePhysical,
                                      &mmu->pageTableLogical);

    if (status < 0)
    {
        /* Roll back. */
        gcmkVERIFY_OK(gckOS_DeleteMutex(os, mmu->mutex));

        mmu->object.type = gcvOBJ_UNKNOWN;
        gcmkVERIFY_OK(gckOS_Free(os, mmu));

        /* Error. */
        gcmkFATAL(
            "%s(%d): could not allocate page table.",
            __FUNCTION__, __LINE__
            );

        gcmkFOOTER();
        return status;
    }

    /* Compute number of entries in page table. */
    mmu->entryCount = mmu->pageTableSize / sizeof(gctUINT32);
    mmu->entry = 0;

    /* Mark the entire page table as available. */
    pageTable = (gctUINT32 *) mmu->pageTableLogical;
    for (i = 0; i < mmu->entryCount; i++)
    {
        pageTable[i] = (gctUINT32)~0;
    }

    /* Set page table address. */
    status = gckVGHARDWARE_SetMMU(hardware, mmu->pageTableLogical);

    if (status < 0)
    {
        /* Free the page table. */
        gcmkVERIFY_OK(gckOS_FreeContiguous(mmu->os,
                                      mmu->pageTablePhysical,
                                      mmu->pageTableLogical,
                                      mmu->pageTableSize));

        /* Roll back. */
        gcmkVERIFY_OK(gckOS_DeleteMutex(os, mmu->mutex));

        mmu->object.type = gcvOBJ_UNKNOWN;
        gcmkVERIFY_OK(gckOS_Free(os, mmu));

        /* Error. */
        gcmkFATAL(
            "%s(%d): could not program page table.",
            __FUNCTION__, __LINE__
            );

        gcmkFOOTER();
        return status;
    }

    /* Return the gckVGMMU object pointer. */
    *Mmu = mmu;

    gcmkTRACE_ZONE(
        gcvLEVEL_INFO, gcvZONE_MMU,
        "%s(%d): %u entries at %p.(0x%08X)\n",
        __FUNCTION__, __LINE__,
        mmu->entryCount,
        mmu->pageTableLogical,
        mmu->pageTablePhysical
        );

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGMMU_Destroy
**
**  Destroy a nAQMMU object.
**
**  INPUT:
**
**      gckVGMMU Mmu
**          Pointer to an gckVGMMU object.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS gckVGMMU_Destroy(
    IN gckVGMMU Mmu
    )
{
    gcmkHEADER_ARG("Mmu=0x%x", Mmu);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);

    /* Free the page table. */
    gcmkVERIFY_OK(gckOS_FreeContiguous(Mmu->os,
                                  Mmu->pageTablePhysical,
                                  Mmu->pageTableLogical,
                                  Mmu->pageTableSize));

    /* Roll back. */
    gcmkVERIFY_OK(gckOS_DeleteMutex(Mmu->os, Mmu->mutex));

    /* Mark the gckVGMMU object as unknown. */
    Mmu->object.type = gcvOBJ_UNKNOWN;

    /* Free the gckVGMMU object. */
    gcmkVERIFY_OK(gckOS_Free(Mmu->os, Mmu));

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

/*******************************************************************************
**
**  gckVGMMU_AllocatePages
**
**  Allocate pages inside the page table.
**
**  INPUT:
**
**      gckVGMMU Mmu
**          Pointer to an gckVGMMU object.
**
**      gctSIZE_T PageCount
**          Number of pages to allocate.
**
**  OUTPUT:
**
**      gctPOINTER * PageTable
**          Pointer to a variable that receives the base address of the page
**          table.
**
**      gctUINT32 * Address
**          Pointer to a variable that receives the hardware specific address.
*/
gceSTATUS gckVGMMU_AllocatePages(
    IN gckVGMMU Mmu,
    IN gctSIZE_T PageCount,
    OUT gctPOINTER * PageTable,
    OUT gctUINT32 * Address
    )
{
    gceSTATUS status;
    gctUINT32 tail, index, i;
    gctUINT32 * table;
    gctBOOL allocated = gcvFALSE;

    gcmkHEADER_ARG("Mmu=0x%x PageCount=0x%x PageTable=0x%x Address=0x%x",
        Mmu, PageCount, PageTable, Address);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
    gcmkVERIFY_ARGUMENT(PageCount > 0);
    gcmkVERIFY_ARGUMENT(PageTable != gcvNULL);
    gcmkVERIFY_ARGUMENT(Address != gcvNULL);

    gcmkTRACE_ZONE(
        gcvLEVEL_INFO, gcvZONE_MMU,
        "%s(%d): %u pages.\n",
        __FUNCTION__, __LINE__,
        PageCount
        );

    if (PageCount > Mmu->entryCount)
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_ERROR, gcvZONE_MMU,
            "%s(%d): page table too small for %u pages.\n",
            __FUNCTION__, __LINE__,
            PageCount
            );

        gcmkFOOTER_NO();
        /* Not enough pages avaiable. */
        return gcvSTATUS_OUT_OF_RESOURCES;
    }

    /* Grab the mutex. */
    status = gckOS_AcquireMutex(Mmu->os, Mmu->mutex, gcvINFINITE);

    if (status < 0)
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_ERROR, gcvZONE_MMU,
            "%s(%d): could not acquire mutex.\n"
            ,__FUNCTION__, __LINE__
            );

        gcmkFOOTER();
        /* Error. */
        return status;
    }

    /* Compute the tail for this allocation. */
    tail = Mmu->entryCount - PageCount;

    /* Walk all entries until we find enough slots. */
    for (index = Mmu->entry; index <= tail;)
    {
        /* Access page table. */
        table = (gctUINT32 *) Mmu->pageTableLogical + index;

        /* See if all slots are available. */
        for (i = 0; i < PageCount; i++, table++)
        {
            if (*table != ~0)
            {
                /* Start from next slot. */
                index += i + 1;
                break;
            }
        }

        if (i == PageCount)
        {
            /* Bail out if we have enough page entries. */
            allocated = gcvTRUE;
            break;
        }
    }

    if (!allocated)
    {
        /* Flush the MMU. */
        status = gckVGHARDWARE_FlushMMU(Mmu->hardware);

        if (status >= 0)
        {
            /* Walk all entries until we find enough slots. */
            for (index = 0; index <= tail;)
            {
                /* Access page table. */
                table = (gctUINT32 *) Mmu->pageTableLogical + index;

                /* See if all slots are available. */
                for (i = 0; i < PageCount; i++, table++)
                {
                    if (*table != ~0)
                    {
                        /* Start from next slot. */
                        index += i + 1;
                        break;
                    }
                }

                if (i == PageCount)
                {
                    /* Bail out if we have enough page entries. */
                    allocated = gcvTRUE;
                    break;
                }
            }
        }
    }

    if (!allocated && (status >= 0))
    {
        gcmkTRACE_ZONE(
            gcvLEVEL_ERROR, gcvZONE_MMU,
            "%s(%d): not enough free pages for %u pages.\n",
            __FUNCTION__, __LINE__,
            PageCount
            );

        /* Not enough empty slots available. */
        status = gcvSTATUS_OUT_OF_RESOURCES;
    }

    if (status >= 0)
    {
        /* Build virtual address. */
        status = gckVGHARDWARE_BuildVirtualAddress(Mmu->hardware,
                                                 index,
                                                 0,
                                                 Address);

        if (status >= 0)
        {
            /* Update current entry into page table. */
            Mmu->entry = index + PageCount;

            /* Return pointer to page table. */
            *PageTable = (gctUINT32 *)  Mmu->pageTableLogical + index;

            gcmkTRACE_ZONE(
                gcvLEVEL_INFO, gcvZONE_MMU,
                "%s(%d): allocated %u pages at index %u (0x%08X) @ %p.\n",
                __FUNCTION__, __LINE__,
                PageCount,
                index,
                *Address,
                *PageTable
                );
            }
    }

    /* Release the mutex. */
    gcmkVERIFY_OK(gckOS_ReleaseMutex(Mmu->os, Mmu->mutex));
    gcmkFOOTER();

    /* Return status. */
    return status;
}

/*******************************************************************************
**
**  gckVGMMU_FreePages
**
**  Free pages inside the page table.
**
**  INPUT:
**
**      gckVGMMU Mmu
**          Pointer to an gckVGMMU object.
**
**      gctPOINTER PageTable
**          Base address of the page table to free.
**
**      gctSIZE_T PageCount
**          Number of pages to free.
**
**  OUTPUT:
**
**      Nothing.
*/
gceSTATUS gckVGMMU_FreePages(
    IN gckVGMMU Mmu,
    IN gctPOINTER PageTable,
    IN gctSIZE_T PageCount
    )
{
    gctUINT32 * table;

    gcmkHEADER_ARG("Mmu=0x%x PageTable=0x%x PageCount=0x%x",
        Mmu, PageTable, PageCount);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
    gcmkVERIFY_ARGUMENT(PageTable != gcvNULL);
    gcmkVERIFY_ARGUMENT(PageCount > 0);

    gcmkTRACE_ZONE(
        gcvLEVEL_INFO, gcvZONE_MMU,
        "%s(%d): freeing %u pages at index %u @ %p.\n",
        __FUNCTION__, __LINE__,
        PageCount,
        ((gctUINT32 *) PageTable - (gctUINT32 *) Mmu->pageTableLogical),
        PageTable
        );

    /* Convert pointer. */
    table = (gctUINT32 *) PageTable;

    /* Mark the page table entries as available. */
    while (PageCount-- > 0)
    {
        *table++ = (gctUINT32)~0;
    }

    gcmkFOOTER_NO();
    /* Success. */
    return gcvSTATUS_OK;
}

gceSTATUS
gckVGMMU_SetPage(
    IN gckVGMMU Mmu,
    IN gctUINT32 PageAddress,
    IN gctUINT32 *PageEntry
    )
{
    gcmkHEADER_ARG("Mmu=0x%x", Mmu);

    /* Verify the arguments. */
    gcmkVERIFY_OBJECT(Mmu, gcvOBJ_MMU);
    gcmkVERIFY_ARGUMENT(PageEntry != gcvNULL);
    gcmkVERIFY_ARGUMENT(!(PageAddress & 0xFFF));

    *PageEntry = PageAddress;

    /* Success. */
    gcmkFOOTER_NO();
    return gcvSTATUS_OK;
}

#endif /* gcdENABLE_VG */
