/*
 *    Initial setup-routines for HP 9000 based hardware.
 *
 *    Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *    Modifications for PA-RISC (C) 1999-2008 Helge Deller <deller@gmx.de>
 *    Modifications copyright 1999 SuSE GmbH (Philipp Rumpf)
 *    Modifications copyright 2000 Martin K. Petersen <mkp@mkp.net>
 *    Modifications copyright 2000 Philipp Rumpf <prumpf@tux.org>
 *    Modifications copyright 2001 Ryan Bradetich <rbradetich@uswest.net>
 *
 *    Initial PA-RISC Version: 04-23-1999 by Helge Deller
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <asm/param.h>
#include <asm/cache.h>
#include <asm/hardware.h>	
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pdc.h>
#include <asm/pdcpat.h>
#include <asm/irq.h>		
#include <asm/parisc-device.h>

struct system_cpuinfo_parisc boot_cpu_data __read_mostly;
EXPORT_SYMBOL(boot_cpu_data);

DEFINE_PER_CPU(struct cpuinfo_parisc, cpu_data);

extern int update_cr16_clocksource(void);	


static void
init_percpu_prof(unsigned long cpunum)
{
	struct cpuinfo_parisc *p;

	p = &per_cpu(cpu_data, cpunum);
	p->prof_counter = 1;
	p->prof_multiplier = 1;
}


static int processor_probe(struct parisc_device *dev)
{
	unsigned long txn_addr;
	unsigned long cpuid;
	struct cpuinfo_parisc *p;

#ifdef CONFIG_SMP
	if (num_online_cpus() >= nr_cpu_ids) {
		printk(KERN_INFO "num_online_cpus() >= nr_cpu_ids\n");
		return 1;
	}
#else
	if (boot_cpu_data.cpu_count > 0) {
		printk(KERN_INFO "CONFIG_SMP=n  ignoring additional CPUs\n");
		return 1;
	}
#endif

	/* logical CPU ID and update global counter
	 * May get overwritten by PAT code.
	 */
	cpuid = boot_cpu_data.cpu_count;
	txn_addr = dev->hpa.start;	

#ifdef CONFIG_64BIT
	if (is_pdc_pat()) {
		ulong status;
		unsigned long bytecnt;
	        pdc_pat_cell_mod_maddr_block_t *pa_pdc_cell;
#undef USE_PAT_CPUID
#ifdef USE_PAT_CPUID
		struct pdc_pat_cpu_num cpu_info;
#endif

		pa_pdc_cell = kmalloc(sizeof (*pa_pdc_cell), GFP_KERNEL);
		if (!pa_pdc_cell)
			panic("couldn't allocate memory for PDC_PAT_CELL!");

		status = pdc_pat_cell_module(&bytecnt, dev->pcell_loc,
			dev->mod_index, PA_VIEW, pa_pdc_cell);

		BUG_ON(PDC_OK != status);

		
		BUG_ON(dev->mod_info != pa_pdc_cell->mod_info);
		BUG_ON(dev->pmod_loc != pa_pdc_cell->mod_location);

		txn_addr = pa_pdc_cell->mod[0];   

		kfree(pa_pdc_cell);

#ifdef USE_PAT_CPUID
		
		status = pdc_pat_cpu_get_number(&cpu_info, dev->hpa.start);

		BUG_ON(PDC_OK != status);

		if (cpu_info.cpu_num >= NR_CPUS) {
			printk(KERN_WARNING "IGNORING CPU at 0x%x,"
				" cpu_slot_id > NR_CPUS"
				" (%ld > %d)\n",
				dev->hpa.start, cpu_info.cpu_num, NR_CPUS);
			
			boot_cpu_data.cpu_count--;
			return 1;
		} else {
			cpuid = cpu_info.cpu_num;
		}
#endif
	}
#endif

	p = &per_cpu(cpu_data, cpuid);
	boot_cpu_data.cpu_count++;

	
	if (cpuid)
		memset(p, 0, sizeof(struct cpuinfo_parisc));

	p->loops_per_jiffy = loops_per_jiffy;
	p->dev = dev;		
	p->hpa = dev->hpa.start;	
	p->cpuid = cpuid;	
	p->txn_addr = txn_addr;	
#ifdef CONFIG_SMP
	init_percpu_prof(cpuid);
#endif


#if 0
	
	if (cpuid) {
		struct irqaction actions[];

		actions = kmalloc(sizeof(struct irqaction)*MAX_CPU_IRQ, GFP_ATOMIC);
		if (!actions) {
			
			actions = cpu_irq_actions[0];
		}

		cpu_irq_actions[cpuid] = actions;
	}
#endif

#ifdef CONFIG_SMP
	if (cpuid) {
		set_cpu_present(cpuid, true);
		cpu_up(cpuid);
	}
#endif

	update_cr16_clocksource();

	return 0;
}

void __init collect_boot_cpu_data(void)
{
	memset(&boot_cpu_data, 0, sizeof(boot_cpu_data));

	boot_cpu_data.cpu_hz = 100 * PAGE0->mem_10msec; 

	
#define p ((unsigned long *)&boot_cpu_data.pdc.model)
	if (pdc_model_info(&boot_cpu_data.pdc.model) == PDC_OK)
		printk(KERN_INFO 
			"model %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
#undef p

	if (pdc_model_versions(&boot_cpu_data.pdc.versions, 0) == PDC_OK)
		printk(KERN_INFO "vers  %08lx\n", 
			boot_cpu_data.pdc.versions);

	if (pdc_model_cpuid(&boot_cpu_data.pdc.cpuid) == PDC_OK)
		printk(KERN_INFO "CPUID vers %ld rev %ld (0x%08lx)\n",
			(boot_cpu_data.pdc.cpuid >> 5) & 127,
			boot_cpu_data.pdc.cpuid & 31,
			boot_cpu_data.pdc.cpuid);

	if (pdc_model_capabilities(&boot_cpu_data.pdc.capabilities) == PDC_OK)
		printk(KERN_INFO "capabilities 0x%lx\n",
			boot_cpu_data.pdc.capabilities);

	if (pdc_model_sysmodel(boot_cpu_data.pdc.sys_model_name) == PDC_OK)
		printk(KERN_INFO "model %s\n",
			boot_cpu_data.pdc.sys_model_name);

	boot_cpu_data.hversion =  boot_cpu_data.pdc.model.hversion;
	boot_cpu_data.sversion =  boot_cpu_data.pdc.model.sversion;

	boot_cpu_data.cpu_type = parisc_get_cpu_type(boot_cpu_data.hversion);
	boot_cpu_data.cpu_name = cpu_name_version[boot_cpu_data.cpu_type][0];
	boot_cpu_data.family_name = cpu_name_version[boot_cpu_data.cpu_type][1];
}



int init_per_cpu(int cpunum)
{
	int ret;
	struct pdc_coproc_cfg coproc_cfg;

	set_firmware_width();
	ret = pdc_coproc_cfg(&coproc_cfg);

	if(ret >= 0 && coproc_cfg.ccr_functional) {
		mtctl(coproc_cfg.ccr_functional, 10);  

		per_cpu(cpu_data, cpunum).fp_rev = coproc_cfg.revision;
		per_cpu(cpu_data, cpunum).fp_model = coproc_cfg.model;

		printk(KERN_INFO  "FP[%d] enabled: Rev %ld Model %ld\n",
			cpunum, coproc_cfg.revision, coproc_cfg.model);

		asm volatile ("fstd    %fr0,8(%sp)");

	} else {
		printk(KERN_WARNING  "WARNING: No FP CoProcessor?!"
			" (coproc_cfg.ccr_functional == 0x%lx, expected 0xc0)\n"
#ifdef CONFIG_64BIT
			"Halting Machine - FP required\n"
#endif
			, coproc_cfg.ccr_functional);
#ifdef CONFIG_64BIT
		mdelay(100);	
		panic("FP CoProc not reported");
#endif
	}

	
	init_percpu_prof(cpunum);

	return ret;
}

int
show_cpuinfo (struct seq_file *m, void *v)
{
	unsigned long cpu;

	for_each_online_cpu(cpu) {
		const struct cpuinfo_parisc *cpuinfo = &per_cpu(cpu_data, cpu);
#ifdef CONFIG_SMP
		if (0 == cpuinfo->hpa)
			continue;
#endif
		seq_printf(m, "processor\t: %lu\n"
				"cpu family\t: PA-RISC %s\n",
				 cpu, boot_cpu_data.family_name);

		seq_printf(m, "cpu\t\t: %s\n",  boot_cpu_data.cpu_name );

		
		seq_printf(m, "cpu MHz\t\t: %d.%06d\n",
				 boot_cpu_data.cpu_hz / 1000000,
				 boot_cpu_data.cpu_hz % 1000000  );

		seq_printf(m, "capabilities\t:");
		if (boot_cpu_data.pdc.capabilities & PDC_MODEL_OS32)
			seq_printf(m, " os32");
		if (boot_cpu_data.pdc.capabilities & PDC_MODEL_OS64)
			seq_printf(m, " os64");
		seq_printf(m, "\n");

		seq_printf(m, "model\t\t: %s\n"
				"model name\t: %s\n",
				 boot_cpu_data.pdc.sys_model_name,
				 cpuinfo->dev ?
				 cpuinfo->dev->name : "Unknown");

		seq_printf(m, "hversion\t: 0x%08x\n"
			        "sversion\t: 0x%08x\n",
				 boot_cpu_data.hversion,
				 boot_cpu_data.sversion );

		
		show_cache_info(m);

		seq_printf(m, "bogomips\t: %lu.%02lu\n",
			     cpuinfo->loops_per_jiffy / (500000 / HZ),
			     (cpuinfo->loops_per_jiffy / (5000 / HZ)) % 100);

		seq_printf(m, "software id\t: %ld\n\n",
				boot_cpu_data.pdc.model.sw_id);
	}
	return 0;
}

static const struct parisc_device_id processor_tbl[] = {
	{ HPHW_NPROC, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, SVERSION_ANY_ID },
	{ 0, }
};

static struct parisc_driver cpu_driver = {
	.name		= "CPU",
	.id_table	= processor_tbl,
	.probe		= processor_probe
};

void __init processor_init(void)
{
	register_parisc_driver(&cpu_driver);
}
