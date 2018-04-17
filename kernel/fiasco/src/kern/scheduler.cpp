INTERFACE:

#include "context.h"
#include "icu_helper.h"
#include "types.h"

class Scheduler : public Icu_h<Scheduler>, public Irq_chip_soft
{
  typedef Icu_h<Scheduler> Icu;

public:
  enum Operation
  {
    Info       = 0,
    Run_thread = 1,
    Idle_time  = 2,
    Deploy_thread = 3,
    Get_rqs = 4,
    Get_dead = 5,
  };

  static Scheduler scheduler;
private:
  Irq_base *_irq;

  L4_RPC(Info,      sched_info, (L4_cpu_set_descr set, Mword *rm, Mword *max_cpus));
  L4_RPC(Idle_time, sched_idle, (L4_cpu_set cpus, Cpu_time *time));
};

// ----------------------------------------------------------------------------
IMPLEMENTATION:

#include "thread_object.h"
#include "l4_buf_iter.h"
#include "l4_types.h"
#include "entry_frame.h"


JDB_DEFINE_TYPENAME(Scheduler, "\033[34mSched\033[m");
Scheduler Scheduler::scheduler;

PUBLIC void
Scheduler::operator delete (void *)
{
  printf("WARNING: tried to delete kernel scheduler object.\n"
         "         The system is now useless\n");
}

PUBLIC inline
Scheduler::Scheduler() : _irq(0)
{
  initial_kobjects.register_obj(this, Initial_kobjects::Scheduler);
}


PRIVATE
L4_msg_tag
Scheduler::sys_run(L4_fpage::Rights, Syscall_frame *f, Utcb const *iutcb, Utcb *outcb)
{
  L4_msg_tag tag = f->tag();
  Cpu_number const curr_cpu = current_cpu();

  if (EXPECT_FALSE(tag.words() < 5))
    return commit_result(-L4_err::EInval);

  unsigned long sz = sizeof (L4_sched_param_legacy);

    {
      L4_sched_param const *sched_param = reinterpret_cast<L4_sched_param const*>(&iutcb->values[1]);
      if (sched_param->sched_class < 0)
        sz = sched_param->length;

      sz += sizeof(Mword) - 1;
      sz /= sizeof(Mword);

      if (sz + 1 > tag.words())
	return commit_result(-L4_err::EInval);
    }

  Ko::Rights rights;
  Thread *thread = Ko::deref<Thread>(&tag, iutcb, &rights);
  if (!thread)
    return tag;


  Mword _store[sz];
  memcpy(_store, &iutcb->values[1], sz * sizeof(Mword));

  L4_sched_param const *sched_param = reinterpret_cast<L4_sched_param const *>(_store);

  Thread::Migration info;

  Cpu_number const t_cpu = thread->home_cpu();

  if (Cpu::online(t_cpu) && sched_param->cpus.contains(t_cpu))
    info.cpu = t_cpu;
  else if (sched_param->cpus.contains(curr_cpu))
    info.cpu = curr_cpu;
  else
    info.cpu = sched_param->cpus.first(Cpu::present_mask(), Config::max_num_cpus());

  //start time
  outcb->values[0] = thread->dbg_id();
  outcb->values[1] = Timer::system_clock();

  /* Own work */
#ifdef CONFIG_SCHED_FP_EDF
  /* edf thread */
  if (iutcb->values[5] > 0)
  {
    L4_sched_param_deadline	      sched_p;
    sched_p.sched_class     = -3;
    /* Add deadline to arrival time */
    sched_p.deadline 	    = (iutcb->values[5])+(outcb->values[1]);
    thread->sched_context()->set(static_cast<L4_sched_param*>(&sched_p));
    sched_param = reinterpret_cast<L4_sched_param const *>(&sched_p);
    info.sp=sched_param;
  }
  else
  {
  /* fp thread */
  if (iutcb->values[3] > 0)
  {
    L4_sched_param_fixed_prio	      sched_p;
    sched_p.sched_class     = -1;
    sched_p.prio 	    = iutcb->values[3];
    thread->sched_context()->set(static_cast<L4_sched_param*>(&sched_p));
    sched_param = reinterpret_cast<L4_sched_param const *>(&sched_p);
    info.sp=sched_param;
  }
  else
  {
    info.sp = sched_param;
  }
  }
#else
  info.sp = sched_param;
#endif

  if (0)
    printf("CPU[%u]: run(thread=%lx, cpu=%u (%lx,%u,%u)\n",
           cxx::int_value<Cpu_number>(curr_cpu), thread->dbg_id(),
           cxx::int_value<Cpu_number>(info.cpu),
           iutcb->values[2],
           cxx::int_value<Cpu_number>(sched_param->cpus.offset()),
           cxx::int_value<Order>(sched_param->cpus.granularity()));

  thread->migrate(&info);

  return commit_result(0,2);
}
PRIVATE
L4_msg_tag
Scheduler::sys_deploy_thread(L4_fpage::Rights, Syscall_frame *f, Utcb const *utcb) //gmc
{
	L4_msg_tag const tag = f->tag();

	Sched_context::Ready_queue &rq = Sched_context::rq.current();

	int list[(int)tag.words()-1];
	list[0]=((int)tag.words()-3)/2;

	for(unsigned i = 1 ; i < tag.words()-1; i++){
		list[i]=utcb->values[i+1];			 
	}

	rq.switch_ready_queue(&list[0]);

	return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::op_sched_idle(L4_cpu_set const &cpus, Cpu_time *time)
{
  Cpu_number const cpu = cpus.first(Cpu::online_mask(), Config::max_num_cpus());
  if (EXPECT_FALSE(cpu == Config::max_num_cpus()))
    return commit_result(-L4_err::EInval);

  *time = Context::kernel_context(cpu)->consumed_time();
  return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::op_sched_info(L4_cpu_set_descr const &s, Mword *m, Mword *max_cpus)
{
  Mword rm = 0;
  Cpu_number max = Config::max_num_cpus();
  Order granularity = s.granularity();
  Cpu_number const offset = s.offset();

  if (offset >= max)
    return commit_result(-L4_err::ERange);

  if (max > offset + Cpu_number(MWORD_BITS) << granularity)
    max = offset + Cpu_number(MWORD_BITS) << granularity;

  for (Cpu_number i = Cpu_number::first(); i < max - offset; ++i)
    if (Cpu::present_mask().get(i + offset))
      rm |= (1 << cxx::int_value<Cpu_number>(i >> granularity));

  *m = rm;
  *max_cpus = Config::Max_num_cpus;
  return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::sys_get_rqs(L4_fpage::Rights, Syscall_frame *f, Utcb const *iutcb, Utcb *outcb)
{
	int info[101];
	info[1]=iutcb->values[1];
	Sched_context::Ready_queue &rq = Sched_context::rq.cpu(Cpu_number(iutcb->values[2]));
	rq.get_rqs(info);
	int num_subjects=info[0];
	outcb->values[0]=num_subjects;
	//printf("Num subjects:%d\n",num_subjects);
	for(int i=1; i<=2*num_subjects; i++)
	{
		//printf("%d\n",info[i]);
		outcb->values[i]=info[i];
	}
	return commit_result(0, (2*info[0])+1);
}

PRIVATE
L4_msg_tag
Scheduler::sys_get_dead(L4_fpage::Rights, Syscall_frame *f, Utcb *outcb)
{
	long long unsigned info[101];
	Sched_context::Ready_queue &rq = Sched_context::rq.current();
	rq.get_dead(info);
	long long unsigned num_subjects=info[0];
	outcb->values[0]=num_subjects;
	for(int i=1; i<=2*num_subjects; i++)
	{
		outcb->values[i]=info[i];
	}
	return commit_result(0, (2*info[0])+1);
}

PUBLIC inline
Irq_base *
Scheduler::icu_get_irq(unsigned irqnum)
{
  if (irqnum > 0)
    return 0;

  return _irq;
}

PUBLIC inline
L4_msg_tag
Scheduler::op_icu_get_info(Mword *features, Mword *num_irqs, Mword *num_msis)
{
  *features = 0; // supported features (only normal irqs)
  *num_irqs = 1;
  *num_msis = 0;
  return L4_msg_tag(0);
}

PUBLIC
L4_msg_tag
Scheduler::op_icu_bind(unsigned irqnum, Ko::Cap<Irq> const &irq)
{
  if (irqnum > 0)
    return commit_result(-L4_err::EInval);

  if (_irq)
    _irq->unbind();

  if (!Ko::check_rights(irq.rights, Ko::Rights::CW()))
    return commit_result(-L4_err::EPerm);

  Irq_chip_soft::bind(irq.obj, irqnum);
  _irq = irq.obj;
  return commit_result(0);
}

PUBLIC
L4_msg_tag
Scheduler::op_icu_set_mode(Mword pin, Irq_chip::Mode)
{
  if (pin != 0)
    return commit_result(-L4_err::EInval);

  if (_irq)
    _irq->switch_mode(true);
  return commit_result(0);
}

PUBLIC inline
void
Scheduler::trigger_hotplug_event()
{
  if (_irq)
    _irq->hit(0);
}

PUBLIC
L4_msg_tag
Scheduler::kinvoke(L4_obj_ref ref, L4_fpage::Rights rights, Syscall_frame *f,
                   Utcb const *iutcb, Utcb *outcb)
{
  switch (f->tag().proto())
    {
    case L4_msg_tag::Label_irq:
      return Icu::icu_invoke(ref, rights, f, iutcb,outcb);
    case L4_msg_tag::Label_scheduler:
      break;
    default:
      return commit_result(-L4_err::EBadproto);
    }

  switch (iutcb->values[0])
    {
    case Info:       return Msg_sched_info::call(this, f->tag(), iutcb, outcb);
    case Run_thread: return sys_run(rights, f, iutcb, outcb);
    case Idle_time:  return Msg_sched_idle::call(this, f->tag(), iutcb, outcb);
    case Deploy_thread: return sys_deploy_thread(rights, f, iutcb);
    case Get_rqs:    return sys_get_rqs(rights, f, iutcb, outcb);
    case Get_dead:   return sys_get_dead(rights, f, outcb);
    default:         return commit_result(-L4_err::ENosys);
    }
}
