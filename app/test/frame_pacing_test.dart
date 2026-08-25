import 'package:flutter_test/flutter_test.dart';
import 'package:odpar_territorial_domain_greenfield/src/engine/frame_pacing.dart';

void main() {
  test('frame pacing window reports p50 p95 p99 and freeze spikes', () {
    final FramePacingWindow window = FramePacingWindow(capacity: 8);
    for (final int value in <int>[10000, 12000, 14000, 16000, 18000, 20000, 22000, 60000]) {
      window.push(value);
    }
    final FramePacingSnapshot snapshot = window.snapshot();
    expect(snapshot.p50Us, 18000);
    expect(snapshot.p95Us, 60000);
    expect(snapshot.p99Us, 60000);
    expect(snapshot.maxUs, 60000);
    expect(snapshot.spikesOver50Ms, 1);
  });
}
