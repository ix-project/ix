#!/usr/bin/env python

# Copyright 2013-16 Board of Trustees of Stanford University
# Copyright 2013-16 Ecole Polytechnique Federale Lausanne (EPFL)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

import argparse
import ctypes
import glob
import mmap
import os
import os.path
import posix_ipc
import struct
import subprocess
import sys
import time
import threading

BITS_PER_LONG = 64
NCPU = 128
ETH_MAX_NUM_FG = 512
NETHDEV = 16
ETH_MAX_TOTAL_FG = ETH_MAX_NUM_FG * NETHDEV
IDLE_FIFO_SIZE = 256

class CpuMetrics(ctypes.Structure):
  _fields_ = [
    ('queuing_delay', ctypes.c_double),
    ('batch_size', ctypes.c_double),
    ('queue_size', ctypes.c_double * 3),
    ('loop_duration', ctypes.c_long),
    ('idle', ctypes.c_double * 3),
    ('padding', ctypes.c_byte * 56),
  ]

class FlowGroupMetrics(ctypes.Structure):
  _fields_ = [
    ('cpu', ctypes.c_uint),
    ('padding', ctypes.c_byte * 60),
  ]

class CmdParamsMigrate(ctypes.Structure):
  _fields_ = [
    ('fg_bitmap', ctypes.c_ulong * (ETH_MAX_TOTAL_FG / BITS_PER_LONG)),
    ('cpu', ctypes.c_uint),
  ]

class CmdParamsIdle(ctypes.Structure):
  _fields_ = [
    ('fifo', ctypes.c_char * IDLE_FIFO_SIZE),
  ]

class CommandParameters(ctypes.Union):
  _fields_ = [
    ('migrate', CmdParamsMigrate),
    ('idle', CmdParamsIdle),
  ]

class Command(ctypes.Structure):
  CP_CMD_NOP = 0
  CP_CMD_MIGRATE = 1
  CP_CMD_IDLE = 2

  CP_STATUS_READY = 0
  CP_STATUS_RUNNING = 1

  CP_CPU_STATE_IDLE = 0
  CP_CPU_STATE_RUNNING = 1

  _fields_ = [
    ('cpu_state', ctypes.c_uint),
    ('cmd_id', ctypes.c_uint),
    ('status', ctypes.c_uint),
    ('cmd_params', CommandParameters),
    ('no_idle', ctypes.c_byte),
  ]

class Scratchpad(ctypes.Structure):
  _fields_ = [
    ('remote_queue_pkts_begin', ctypes.c_long),
    ('remote_queue_pkts_end', ctypes.c_long),
    ('local_queue_pkts', ctypes.c_long),
    ('backlog_before', ctypes.c_long),
    ('backlog_after', ctypes.c_long),
    ('timers', ctypes.c_long),
    ('timer_fired', ctypes.c_long),
    ('ts_migration_start', ctypes.c_long),
    ('ts_data_structures_done', ctypes.c_long),
    ('ts_before_backlog', ctypes.c_long),
    ('ts_after_backlog', ctypes.c_long),
    ('ts_migration_end', ctypes.c_long),
    ('ts_first_pkt_at_prev', ctypes.c_long),
    ('ts_last_pkt_at_prev', ctypes.c_long),
    ('ts_first_pkt_at_target', ctypes.c_long),
    ('ts_last_pkt_at_target', ctypes.c_long),
  ]

class ShMem(ctypes.Structure):
  _fields_ = [
    ('nr_flow_groups', ctypes.c_uint),
    ('nr_cpus', ctypes.c_uint),
    ('pkg_power', ctypes.c_float),
    ('cpu', ctypes.c_int * NCPU),
    ('padding', ctypes.c_byte * 52),
    ('cpu_metrics', CpuMetrics * NCPU),
    ('flow_group', FlowGroupMetrics * ETH_MAX_TOTAL_FG),
    ('command', Command * NCPU),
    ('cycles_per_us', ctypes.c_uint),
    ('scratchpad_idx', ctypes.c_uint),
    ('scratchpad', Scratchpad * 1024),
  ]

def bitmap_create(size, on):
  bitmap = [0] * (size / BITS_PER_LONG)

  for pos in on:
    bitmap[pos / BITS_PER_LONG] |= 1 << (pos % BITS_PER_LONG)

  return bitmap

def migrate(shmem, source_cpu, target_cpu, flow_groups):
  cmd = shmem.command[source_cpu]
  cmd.no_idle = 1
  bitmap = bitmap_create(ETH_MAX_TOTAL_FG, flow_groups)
  cmd.cmd_params.migrate.fg_bitmap = (ctypes.c_ulong * len(bitmap))(*bitmap)
  cmd.cmd_params.migrate.cpu = target_cpu
  cmd.status = Command.CP_STATUS_RUNNING
  cmd.cmd_id = Command.CP_CMD_MIGRATE
  while cmd.status != Command.CP_STATUS_READY:
    pass
  cmd.no_idle = 0

def get_fifo(cpu):
  return os.path.abspath('block-%d.fifo' % cpu)

def is_idle(cpu):
  return os.path.exists(get_fifo(cpu))

def idle(shmem, cpu):
  if is_idle(cpu):
    return
  fifo = get_fifo(cpu)
  os.mkfifo(fifo)

  cmd = shmem.command[cpu]
  assert len(fifo) + 1 < IDLE_FIFO_SIZE, fifo
  cmd.cmd_params.idle.fifo = fifo
  cmd.status = Command.CP_STATUS_RUNNING
  cmd.cmd_id = Command.CP_CMD_IDLE
  while cmd.status != Command.CP_STATUS_READY:
    pass

def wake_up(shmem, cpu):
  if not is_idle(cpu):
    return
  fifo = get_fifo(cpu)
  fd = os.open(fifo, os.O_WRONLY)
  os.write(fd, '1')
  os.close(fd)
  os.remove(fifo)
  cmd = shmem.command[cpu]
  while cmd.cpu_state != Command.CP_CPU_STATE_RUNNING:
    pass

def set_nr_cpus(shmem, fg_per_cpu, cpu_count, verbose = False):
  cpus = cpu_lists.ht_interleaved[:cpu_count]
  return set_cpus(shmem, fg_per_cpu, cpus, verbose)

def set_cpulist(shmem, fg_per_cpu, cpulist, verbose = False):
  reverse_map = {}
  for i in xrange(shmem.nr_cpus):
    reverse_map[shmem.cpu[i]] = i

  cpus = []
  for cpu in cpulist:
    if cpu in reverse_map:
      cpus.append(reverse_map[cpu])
    else:
      print >>sys.stderr, 'Invalid cpulist'
      return
  return set_cpus(shmem, fg_per_cpu, cpus, verbose)

def list_runs_to_str(inp):
  if len(inp) == 0:
    return '0:[]'

  runa = min(inp)
  runb = min(inp)
  ret = []
  for i in xrange(min(inp)+1, max(inp)+2):
    if i not in inp:
      if runa is not None:
        if runa == runb:
          ret.append('%d' % runa)
        else:
          ret.append('%d-%d' % (runa, runb))
        runa = None
    elif runa is None:
      runa = i
      runb = i
    else:
      runb = i
  return '%d:[%s]' % (len(inp),','.join(ret))

def set_cpus(shmem, fg_per_cpu, cpus, verbose = False):
  global migration_times

  fgs_per_cpu = int(shmem.nr_flow_groups / len(cpus))
  one_more_fg = shmem.nr_flow_groups % len(cpus)

  def fgs_at(cpu):
    fgs = fgs_per_cpu
    if cpus.index(cpu) < one_more_fg:
      fgs += 1
    return fgs

  migration_times = []
  start = 0
  for target_cpu in cpus:
    shmem.command[target_cpu].no_idle = 1
    wake_up(shmem, target_cpu)

    for source_cpu in xrange(NCPU):
      if source_cpu == target_cpu:
        continue
      count = min(fgs_at(target_cpu)-len(fg_per_cpu[target_cpu]), len(fg_per_cpu[source_cpu]))
      if source_cpu in cpus:
        count = min(count, len(fg_per_cpu[source_cpu])-fgs_at(source_cpu))
      if count <= 0:
        continue
      intersection = set(fg_per_cpu[source_cpu][-count:])
      #print 'migrate from %d to %d fgs %r' % (source_cpu, target_cpu, list(intersection))
      start_time = time.time()
      migrate(shmem, source_cpu, target_cpu, list(intersection))
      stop_time = time.time()
      #if verbose:
      #  sys.stdout.write('.')
      #  sys.stdout.flush()
      fg_per_cpu[source_cpu] = list(set(fg_per_cpu[source_cpu]) - intersection)
      fg_per_cpu[target_cpu] = list(set(fg_per_cpu[target_cpu]) | intersection)
      migration_times.append((stop_time - start_time) * 1000)

    shmem.command[target_cpu].no_idle = 0

  if verbose:
    if len(migration_times) > 0:
      print '# migration duration min/avg/max = %f/%f/%f ms (%r)' % (min(migration_times), sum(migration_times)/len(migration_times), max(migration_times),  migration_times)
  for cpu in xrange(shmem.nr_cpus):
    if len(fg_per_cpu[cpu]) == 0:
      idle(shmem, cpu)
  for cpu in xrange(NCPU):
    if len(fg_per_cpu[cpu]) == 0:
      continue
    print '# CPU %02d: flow groups: %s' % (cpu, list_runs_to_str(fg_per_cpu[cpu]))

STEPS_MODE_ENERGY_EFFICIENCY = 1
STEPS_MODE_BACKGROUND_TASK = 2
STEPS_MODE_MINMAX = 3

f = open('/sys/devices/system/cpu/cpu0/topology/core_siblings_list', 'r')
core_count = len(f.readline().split(',')) / 2
f.close()

def get_steps(mode):
  f = open('/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies', 'r')
  frequencies = sorted(map(int, f.readline().split()))
  f.close()

  cpu_list = cpu_lists.ht_at_the_end

  steps = []
  if mode == STEPS_MODE_ENERGY_EFFICIENCY:
    for cpus in xrange(1, core_count + 1):
      steps.append({'cpus': cpu_list[:cpus], 'frequency': frequencies[0]})
    for freq in frequencies:
      steps.append({'cpus': cpu_list[:core_count * 2], 'frequency': freq})
  elif mode == STEPS_MODE_BACKGROUND_TASK:
    for cpus in xrange(1, core_count + 1):
      steps.append({'cpus': cpu_list[:cpus] + cpu_list[core_count:core_count + cpus], 'frequency': frequencies[-2]})
    steps.append({'cpus': cpu_list[:core_count * 2], 'frequency': frequencies[-1]})
  elif mode == STEPS_MODE_MINMAX:
    steps.append({'cpus': [0], 'frequency': frequencies[0]})
    steps.append({'cpus': cpu_list[:core_count * 2], 'frequency': frequencies[-1]})

  return steps

def calculate_idle_threshold(steps):
  turbo_frequency = max(step['frequency'] for step in steps)

  idle_threshold = [2]
  for i in xrange(1, len(steps)):
    step = steps[i]
    prv = steps[i-1]
    if len(step['cpus']) == core_count * 2 and len(prv['cpus']) == core_count:
      idle_threshold.append(1-1/1.3)
    elif len(step['cpus']) != len(prv['cpus']):
      idle_threshold.append(1.0/len([1 for cpu in step['cpus'] if cpu < core_count]))
    elif step['frequency'] != turbo_frequency:
      idle_threshold.append(1.0 * (step['frequency'] - prv['frequency']) / step['frequency'])
    else:
      idle_threshold.append(0.1)

  for i in xrange(len(idle_threshold)):
    idle_threshold[i] *= 1.2

  return idle_threshold

def control_background_job(args, cpus):
  if args.background_cpus is None:
    return
  bg_threads = max(0, len(args.background_cpus) - cpus)
  bg_mask = 0
  for i in xrange(bg_threads):
    bg_mask |= 1 << args.background_cpus[i]
  if args.background_fifo is not None:
    fd = os.open(args.background_fifo, os.O_WRONLY)
    os.write(fd, '%d\n' % bg_threads)
    os.close(fd)
  if args.background_pid is not None and bg_mask != 0:
    DEVNULL = open(os.devnull, 'wb')
    subprocess.check_call(['taskset', '-ap', '%x' % bg_mask, str(args.background_pid)], stdout=DEVNULL)
    DEVNULL.close()
  print '# bg_task threads=%d mask=%x' % (bg_threads, bg_mask)

STEP_UP = 1
STEP_DOWN = 2
def set_step(shmem, fg_per_cpu, step, dir, args):
  global set_step_done

  if dir == STEP_UP:
    control_background_job(args, len([1 for cpu in step['cpus'] if cpu < core_count]))

  for directory in glob.glob('/sys/devices/system/cpu/cpu*/cpufreq/'):
    f = open('%s/scaling_governor' % directory, 'w')
    f.write('userspace\n')
    f.close()
    f = open('%s/scaling_setspeed' % directory, 'w')
    f.write('%s\n' % step['frequency'])
    f.close()
  set_cpus(shmem, fg_per_cpu, step['cpus'])
  set_step_done = True

  if dir == STEP_DOWN:
    control_background_job(args, len([1 for cpu in step['cpus'] if cpu < core_count]))

def get_all_metrics(shmem, attr):
  ret = []
  for cpu in xrange(shmem.nr_cpus):
    if shmem.command[cpu].cpu_state == Command.CP_CPU_STATE_RUNNING:
      ret.append(getattr(shmem.cpu_metrics[cpu], attr))
  return ret

def avg(list):
  return sum(list) / len(list)

class CpuLists:
  pass

cpu_lists = CpuLists()

def compute_cpu_lists(shmem):
  reverse_map = {}
  for i in xrange(shmem.nr_cpus):
    reverse_map[shmem.cpu[i]] = i

  cpu_lists.ht_interleaved = []
  cpu_lists.ht_at_the_end = []
  later = []

  for i in xrange(shmem.nr_cpus):
    if i in later:
      continue
    cpu_lists.ht_interleaved.append(i)
    cpu_lists.ht_at_the_end.append(i)
    f = open('/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list' % shmem.cpu[i], 'r')
    hyperthreads = map(int, f.read().split(','))
    f.close()
    for cpu in hyperthreads:
      if cpu not in reverse_map or reverse_map[cpu] == i:
        continue
      cpu_lists.ht_interleaved.append(reverse_map[cpu])
      later.append(reverse_map[cpu])
  cpu_lists.ht_at_the_end.extend(later)

def main():
  global set_step_done
  global migration_times
  shm = posix_ipc.SharedMemory('/ix', 0)
  buffer = mmap.mmap(shm.fd, ctypes.sizeof(ShMem), mmap.MAP_SHARED, mmap.PROT_WRITE)
  shmem = ShMem.from_buffer(buffer)
  fg_per_cpu = {}
  for i in xrange(NCPU):
    fg_per_cpu[i] = []
  for i in xrange(shmem.nr_flow_groups):
    cpu = shmem.flow_group[i].cpu
    fg_per_cpu[cpu].append(i)

  """
  print 'flow group assignments:'
  for cpu in xrange(NCPU):
    if len(fg_per_cpu[cpu]) == 0:
      continue
    print '  CPU %02d: flow groups %r' % (cpu, fg_per_cpu[cpu])
  print 'commands running at:',
  empty = True
  for i in xrange(NCPU):
    if shmem.command[i].status != Command.CP_STATUS_READY:
      print 'CPU %02d,' % i,
      empty = False
  if empty:
    print 'none',
  print
  """

  parser = argparse.ArgumentParser()
  parser.add_argument('--single-cpu', action='store_true')
  parser.add_argument('--cpus', type=int)
  parser.add_argument('--cpulist', type=str)
  parser.add_argument('--idle', type=int)
  parser.add_argument('--wake-up', type=int)
  parser.add_argument('--show-metrics', action='store_true')
  parser.add_argument('--control', type=str, choices=['eff', 'back', 'minmax'])
  parser.add_argument('--background-fifo', type=str)
  parser.add_argument('--background-pid', type=int)
  parser.add_argument('--background-cpus', type=str)
  parser.add_argument('--print-power', action='store_true')
  parser.add_argument('--print-queues', action='store_true')
  args = parser.parse_args()

  if args.background_cpus is not None:
    args.background_cpus = map(int, args.background_cpus.split(','))

  compute_cpu_lists(shmem)

  if args.single_cpu:
    target_cpu = 0
    for cpu in xrange(shmem.nr_cpus):
      if cpu == target_cpu:
        continue
      migrate(shmem, cpu, target_cpu, fg_per_cpu[cpu])
      sys.stdout.write('.')
      sys.stdout.flush()
    print
  elif args.cpus is not None:
    set_nr_cpus(shmem, fg_per_cpu, args.cpus, verbose = True)
  elif args.cpulist is not None:
    cpulist = map(int, args.cpulist.split(','))
    set_cpulist(shmem, fg_per_cpu, cpulist)
  elif args.idle is not None:
    idle(shmem, args.idle)
  elif args.wake_up is not None:
    wake_up(shmem, args.wake_up)
  elif args.show_metrics:
    for cpu in xrange(shmem.nr_cpus):
      print 'CPU %d: queuing delay: %d us, batch size: %d pkts' % (cpu, shmem.cpu_metrics[cpu].queuing_delay, shmem.cpu_metrics[cpu].batch_size)
  elif args.control is not None:
    if args.control == 'eff':
      mode = STEPS_MODE_ENERGY_EFFICIENCY
    elif args.control == 'back':
      mode = STEPS_MODE_BACKGROUND_TASK
    elif args.control == 'minmax':
      mode = STEPS_MODE_MINMAX
    steps = get_steps(mode)
    idle_threshold = calculate_idle_threshold(steps)
    curr_step_idx = 0
    new_step_idx = curr_step_idx
    set_step(shmem, fg_per_cpu, steps[curr_step_idx], STEP_UP, args)

    last_up = 0
    last_down = 0
    printed_done = True
    set_step_done = True
    migration_times = []
    while True:
      now = time.time()
      print now,
      fast_queue_size = max([x[0] for x in get_all_metrics(shmem, 'queue_size')])
      slow_queue_size = avg([x[2] for x in get_all_metrics(shmem, 'queue_size')])
      idle = avg([x[0] for x in get_all_metrics(shmem, 'idle')])
      print fast_queue_size,
      print avg([x[1] for x in get_all_metrics(shmem, 'queue_size')]),
      print slow_queue_size,
      print idle,
      print avg([x[1] for x in get_all_metrics(shmem, 'idle')]),
      print avg([x[2] for x in get_all_metrics(shmem, 'idle')]),
      print avg(get_all_metrics(shmem, 'loop_duration')),
      print
      if fast_queue_size > 32 and curr_step_idx < len(steps) - 1 and now - last_up >= .2 and now - last_down >= 2:
        new_step_idx = curr_step_idx + 1
      elif slow_queue_size < 8 and idle > idle_threshold[curr_step_idx] and curr_step_idx > 0 and now - last_up >= 4 and now - last_down >= 4:
        new_step_idx = curr_step_idx - 1
      new_step_idx = int(new_step_idx)
      if set_step_done and not printed_done:
        print '# %f control_done' % (now,)
        if len(migration_times) > 0:
          print '# %f migration duration min/avg/max = %f/%f/%f ms (%r)' % (now, min(migration_times), sum(migration_times)/len(migration_times), max(migration_times),  migration_times)
          migration_times = []
          for i in xrange(scratchpad_idx_prv, shmem.scratchpad_idx):
            s = shmem.scratchpad[i]
            print '# migration %d remote_queue_pkts_begin %d remote_queue_pkts_end %d local_queue_pkts %d backlog_before %d backlog_after %d timers %d' % (i, s.remote_queue_pkts_begin, s.remote_queue_pkts_end, s.local_queue_pkts, s.backlog_before, s.backlog_after, s.timers),
            print ' total %d'   % ((s.ts_migration_end - s.ts_migration_start           ) / shmem.cycles_per_us),
            print ' structs %d' % ((s.ts_data_structures_done - s.ts_migration_start    ) / shmem.cycles_per_us),
            print ' var1 %d'    % ((s.ts_first_pkt_at_target - s.ts_data_structures_done) / shmem.cycles_per_us),
            print ' rpc %d'     % ((s.ts_before_backlog - s.ts_first_pkt_at_target      ) / shmem.cycles_per_us),
            print ' backlog %d' % ((s.ts_after_backlog - s.ts_before_backlog            ) / shmem.cycles_per_us),
            print ' lastprv %d' % ((s.ts_last_pkt_at_prev - s.ts_data_structures_done   ) / shmem.cycles_per_us),
            print ' timer_fired %d' % s.timer_fired,
            print

            shmem.scratchpad[i].remote_queue_pkts = 0
            shmem.scratchpad[i].local_queue_pkts = 0
            shmem.scratchpad[i].backlog = 0
            shmem.scratchpad[i].migration_duration = 0
        printed_done = True
      if curr_step_idx != new_step_idx and set_step_done:
        if new_step_idx > curr_step_idx:
          step = 'up'
          last_up = now
          dir = STEP_UP
        else:
          step = 'down'
          last_down = now
          dir = STEP_DOWN
        curr_step_idx = new_step_idx
        scratchpad_idx_prv = shmem.scratchpad_idx
        set_step_done = False
        printed_done = False
        thread = threading.Thread(target=set_step, args=(shmem, fg_per_cpu, steps[curr_step_idx], dir, args))
        thread.daemon = True
        thread.start()
        print '# %f control_action %s step=%d x=x freq=%d cpus=%r x=x' % (now, step,curr_step_idx,steps[curr_step_idx]['frequency'],steps[curr_step_idx]['cpus'])
      time.sleep(.1)
  elif args.print_power:
    print shmem.pkg_power
  elif args.print_queues:
    for cpu in xrange(shmem.nr_cpus):
      if shmem.command[cpu].cpu_state == Command.CP_CPU_STATE_RUNNING:
        q = shmem.cpu_metrics[cpu].queue_size
        print '%d %f/%f/%f' % (cpu, q[0], q[1], q[2])

if __name__ == '__main__':
  main()
