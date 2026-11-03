"""Offline command-level accounting and threshold comparison; no processes run."""
from pathlib import Path
import collections,json,statistics,sys
LAB=Path(__file__).resolve().parent;ROOT=LAB.parents[2];OUT=ROOT/'docs/evidence/m1/M12B_CADENCE_49'
from matrix import commands,rows,dump
def identity(c):return (c['generation'],c['seq'])
def stats(t):
    t=sorted(t);gaps=[(b-a)/1000 for a,b in zip(t,t[1:])]
    def pct(p):return sorted(gaps)[min(len(gaps)-1,int((len(gaps)-1)*p))] if gaps else None
    return dict(count=len(t),hz=(len(t)-1)*1e6/(t[-1]-t[0]) if len(t)>1 and t[-1]>t[0] else None,p50_ms=pct(.5),p95_ms=pct(.95),p99_ms=pct(.99),max_ms=max(gaps) if gaps else None)
def analyze(p):
    meta=json.loads((p/'META.json').read_text());assert meta['status']=='COMPLETE'
    cs=rows(p/'client/client.jsonl');cq=rows(p/'client/qa.jsonl')+rows(p/'restart/qa.jsonl');h=rows(p/'host/host.jsonl');hq=rows(p/'host/qa.jsonl');px=rows(p/'proxy.jsonl')
    timeline=meta['timeline'];begin=next(r['us'] for r in timeline if r['stage']=='measure_begin');end=next(r['us'] for r in timeline if r['stage']=='measure_end')
    q=[r for r in cq if r['kind']=='queued'];wire=[r for r in cq if r['kind']=='udp_send' and r['result']>0];drop=[r for r in cq if r['kind']=='throttle_drop']
    obs=[r for r in px if r['kind']=='observed' and r['direction']=='c2s'];fwd=[r for r in px if r['kind']=='forward' and r['direction']=='c2s']
    evt=[r for r in hq if r['kind']=='host_event'];acc=[r for r in h if r['event']=='network_input_accepted']
    # Expand commands independently at each transport layer; a UDP datagram can
    # contain zero, one or multiple INPUTs. Never equate these unit counts.
    def expand(records):return [(identity(c),r['us'],c['forward']) for r in records for c in commands(bytes.fromhex(r['data']))]
    queued=expand([r for r in q if r['result']==0]);discard=expand(drop);sent=expand(wire);observed=expand(obs)
    dropped=expand([r for r in obs if r['dropped']]);forwarded=expand(fwd);decoded=expand(evt)
    accepted=[((r['generation'],r['input_seq']),r['us'],r['forward']) for r in acc]
    generated=[r for r in cs if r['event']=='sent']
    def count(values):return collections.Counter(r[0] for r in values)
    checks={}
    if cq:
        checks['generated_equals_queue_attempts']=len(generated)==len(q)
        checks['queue_success_equals_wire_plus_local_drop']=count(queued)==count(sent)+count(discard)
        checks['wire_equals_proxy_observed_commands']=count(sent)==count(observed)
        checks['socket_datagrams_equal_proxy_observed_datagrams']=len(wire)==len(obs)
    checks['proxy_command_conservation']=count(observed)==count(dropped)+count(forwarded)
    checks['host_decoded_subset_forwarded']=not (count(decoded)-count(forwarded))
    checks['accepted_subset_decoded']=not (count(accepted)-count(decoded))
    checks['queue_flags_preserved']=all(r['flags']==0 for r in q)
    checks['no_host_affinity_fault']=all(r['affinity_violations']==0 and r['lost_on_stop']==0 for r in h if r['event']=='network_joined')
    untransmitted=count(queued)-count(sent)-count(discard)
    def during(values):return [t for _,t,_ in values if begin<=t<=end]
    cand={str(n):[r for r in h if r['event']=='candidate_timeout' and r['candidate_ticks']==n and begin<=r['us']<=end] for n in [6,8,10]}
    qtime={i:t for i,t,_ in queued};wtime={i:t for i,t,_ in sent};delays=[(wtime[i]-t)/1000 for i,t in qtime.items() if i in wtime and begin<=t<=end]
    costs=next((r for r in cs if r['event']=='scheduler_cost'),{})
    m=dict(run=p.name,spec=meta['spec'],client_sha256=meta['client_sha256'],accounting_checks=checks,
       all_counts=dict(generated=len(generated),queue_attempts=len(q) if cq else None,queue_success=len(queued) if cq else None,queue_failed=sum(r['result']!=0 for r in q) if cq else None,local_throttle_discard=len(discard) if cq else None,
        client_udp_datagrams=len(wire) if cq else None,client_udp_bytes=sum(r['bytes'] for r in wire) if cq else None,udp_embedded_input=len(sent) if cq else None,
        proxy_observed_datagrams=len(obs),proxy_observed_input=len(observed),proxy_dropped_input=len(dropped),proxy_forwarded_input=len(forwarded),host_decoded_input=len(decoded),session_accepted_input=len(accepted),
        forwarded_not_dispatched_by_ENet=sum((count(forwarded)-count(decoded)).values()),decoded_not_accepted_by_Session=sum((count(decoded)-count(accepted)).values()),queued_but_not_socket_or_throttle_observed=sum(untransmitted.values())),
       untransmitted_ids=[dict(generation=k[0],seq=k[1],count=v) for k,v in untransmitted.items()],
       untransmitted_status='INFERRED terminal peer queue reset; QA build did not instrument reset_outgoing_commands' if untransmitted else 'none',
       queue_cadence=stats(during(queued)) if cq else None,host_decode_cadence=stats(during(decoded)),host_accepted_cadence=stats(during(accepted)),
       queue_to_UDP_ms=dict(p50=statistics.median(delays),p95=sorted(delays)[int(.95*(len(delays)-1))],max=max(delays)) if delays else None,
       measured_udp_datagrams=sum(begin<=r['us']<=end for r in wire) if cq else None,
       measured_udp_bytes=sum(r['bytes'] for r in wire if begin<=r['us']<=end) if cq else None,
       measured_IPv4_UDP_bytes_per_second_estimate=sum(r['bytes']+28 for r in wire if begin<=r['us']<=end)/((end-begin)/1e6) if cq else None,
       min_throttle=min((r['throttle'] for r in q),default=None),false_dropouts={n:len(v) for n,v in cand.items()},timeout_events=cand,
       total_CPU_one_core_percent=meta['measure_cpu_seconds']/((end-begin)/1e6)*100,scheduler_cost=costs,
       udp_input_datagrams=sum(bool(commands(bytes.fromhex(r['data']))) for r in wire) if cq else None,
       simultaneous_input_batch_max=max((len(commands(bytes.fromhex(r['data']))) for r in wire),default=0) if cq else None,
       limitations=['4 second measurement holds; synthetic loopback only; seeded decisions repeat, OS timing is not deterministic.',
         'ENet sequenced receive may suppress reordered commands; Worker mailbox may coalesce state. They are separately accounted.',
         'Stock client queue boundary is uninstrumented; generated marker is dispatch only, queue acceptance not assumed.'])
    # 'All counts' include handshake/functional tails only where type=INPUT;
    # cadence and false dropouts use the exact measured hold window.
    dump(p/'ANALYSIS.json',m);return m
def main():
    result=[]
    for p in sorted(OUT.glob('NET_*')):
        if not (p/'META.json').exists():continue
        meta=json.loads((p/'META.json').read_text())
        if meta['status']!='COMPLETE':continue
        result.append(analyze(p))
    dump(OUT/'MATRIX_ANALYSIS.json',result)
    print('runs',len(result),'failed_accounting',[(r['run'],[k for k,v in r['accounting_checks'].items() if not v]) for r in result if not all(r['accounting_checks'].values())])
    for r in result:print(r['run'],round((r['queue_cadence'] or r['host_decode_cadence'])['hz'] or 0,2),r['false_dropouts'],'throttle',r['min_throttle'],'drop',r['all_counts']['local_throttle_discard'])
if __name__=='__main__':main()
