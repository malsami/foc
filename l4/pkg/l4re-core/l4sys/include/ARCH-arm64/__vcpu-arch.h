/*
 * (c) 2009 Adam Lackorzynski <adam@os.inf.tu-dresden.de>,
 *          Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 *
 * As a special exception, you may use this file as part of a free software
 * library without restriction.  Specifically, if other files instantiate
 * templates or use macros or inline functions from this file, or you compile
 * this file and link it with other files to produce an executable, this
 * file does not by itself cause the resulting executable to be covered by
 * the GNU General Public License.  This exception does not however
 * invalidate any other reasons why the executable file might be covered by
 * the GNU General Public License.
 */
#pragma once

#include <l4/sys/types.h>
#include <l4/sys/utcb.h>
#include <l4/sys/__vcpu-arm.h>

enum
{
  /**
   * Architecture specific version ID.
   *
   * This ID must match the version field in the l4_vcpu_state_t structure
   * after enabling vCPU mode or extended vCPU mode for a thread.
   */
  L4_VCPU_STATE_VERSION = 0x33
};

/**
 * \brief vCPU registers.
 * \ingroup l4_vcpu_api
 */
typedef l4_exc_regs_t l4_vcpu_regs_t;

typedef struct l4_vcpu_arch_state_t
{
  l4_umword_t host_tpidruro;
  l4_umword_t user_tpidruro;
} l4_vcpu_arch_state_t;

/**
 * \brief vCPU message registers.
 * \ingroup l4_vcpu_api
 */
typedef struct l4_vcpu_ipc_regs_t
{
  l4_msgtag_t tag;
  l4_umword_t _d1[3];
  l4_umword_t label;
} l4_vcpu_ipc_regs_t;

/**
 * IDs for extended vCPU state fields.
 *
 * Bits 14..15: are the field size:
 *  * 0 = 32bit field
 *  * 1 = register width field
 *  * 2 = 64bit field
 */
enum L4_vcpu_e_field_ids
{
  L4_VCPU_E_HCR        = 0x8000,
  L4_VCPU_E_SCTLR      = 0x0008,
  L4_VCPU_E_CNTKCTL    = 0x000c,
  L4_VCPU_E_MDCR       = 0x0010,
  L4_VCPU_E_MDSCR      = 0x0014,
  L4_VCPU_E_CPACR      = 0x0018,

  L4_VCPU_E_GIC_HCR    = 0x0030,
  L4_VCPU_E_GIC_VTR    = 0x0034,
  L4_VCPU_E_GIC_VMCR   = 0x0038,
  L4_VCPU_E_GIC_MISR   = 0x003c,
  L4_VCPU_E_GIC_EISR0  = 0x0040,
  L4_VCPU_E_GIC_EISR1  = 0x0044,
  L4_VCPU_E_GIC_ELSR0  = 0x0048,
  L4_VCPU_E_GIC_ELSR1  = 0x004c,
  L4_VCPU_E_GIC_APR    = 0x0050,
  L4_VCPU_E_GIC_LR0    = 0x0054,
  L4_VCPU_E_GIC_LR1    = 0x0058,
  L4_VCPU_E_GIC_LR2    = 0x005c,
  L4_VCPU_E_GIC_LR3    = 0x0060,

  L4_VCPU_E_VMPIDR     = 0x8068,
  L4_VCPU_E_CNTVOFF    = 0x8070,
  L4_VCPU_E_CNTVCVAL   = 0x8078,

  L4_VCPU_E_CNTVCTL    = 0x0084,
};