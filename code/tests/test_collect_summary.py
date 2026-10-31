"""Diagnostic activity windows must retain idle/drain and exclude reset spans."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from collect_performance import summarize

def row(t,rx,shown,firmware='test',uptime=None):
 return dict(elapsed_s=t,status=dict(firmware=firmware,capture=dict(frames=rx),
  local_display=dict(decoded=shown,shown=shown,performance=dict(sampled_uptime_ms=uptime if uptime is not None else t*1000))))

class SummaryTests(unittest.TestCase):
 def test_idle_input_and_drain(self):
  result=summarize([row(0,0,0),row(8,0,0),row(10,20,12),row(11,20,16)])
  self.assertEqual(result['valid_interval_seconds'],11)
  self.assertEqual(result['totals']['shown'],16)
  active=result['input_active_intervals']
  self.assertEqual((active['count'],active['seconds']),(1,2))
  self.assertEqual(active['fps']['displayed'],6)
  self.assertEqual(active['totals']['shown'],12)
  self.assertEqual(active['fps']['received'],10)
 def test_reset_and_firmware_boundaries(self):
  result=summarize([row(0,100,50),row(1,0,0),row(2,10,5),row(3,20,10,'other'),row(4,30,15,'other')])
  self.assertEqual(result['invalid_or_reset_intervals'],2)
  self.assertEqual(result['totals']['shown'],10)
  self.assertEqual(result['input_active_intervals']['seconds'],2)
  self.assertEqual(result['input_active_intervals']['fps']['displayed'],5)
 def test_no_input_or_records(self):
  for records in ([],[row(0,1,1)],[row(0,0,0),row(1,0,0)]):
   result=summarize(records)['input_active_intervals']
   self.assertEqual(result['count'],0)
   self.assertIsNone(result['fps']['displayed'])
 def test_uptime_rollback(self):
  result=summarize([row(1,10,5,uptime=2000),row(2,20,10,uptime=1000)])
  self.assertEqual(result['invalid_or_reset_intervals'],1)
  self.assertEqual(result['input_active_intervals']['count'],0)

if __name__=='__main__':unittest.main()
