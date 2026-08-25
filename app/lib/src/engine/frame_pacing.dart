import 'package:flutter/foundation.dart';
import 'package:flutter/scheduler.dart';

@immutable
final class FramePacingSnapshot {
  const FramePacingSnapshot({
    required this.p50Us,
    required this.p95Us,
    required this.p99Us,
    required this.maxUs,
    required this.spikesOver50Ms,
  });

  final int p50Us;
  final int p95Us;
  final int p99Us;
  final int maxUs;
  final int spikesOver50Ms;
}

final class FramePacingWindow {
  FramePacingWindow({this.capacity = 240}) : assert(capacity > 0);

  final int capacity;
  final List<int> _samples = <int>[];
  int _cursor = 0;
  int _maxUs = 0;
  int _spikesOver50Ms = 0;

  void push(int frameTimeUs) {
    final int value = frameTimeUs < 0 ? 0 : frameTimeUs;
    if (value > _maxUs) _maxUs = value;
    if (value > 50000) _spikesOver50Ms += 1;
    if (_samples.length < capacity) {
      _samples.add(value);
    } else {
      _samples[_cursor] = value;
      _cursor = (_cursor + 1) % capacity;
    }
  }

  FramePacingSnapshot snapshot() => FramePacingSnapshot(
        p50Us: _quantile(50),
        p95Us: _quantile(95),
        p99Us: _quantile(99),
        maxUs: _maxUs,
        spikesOver50Ms: _spikesOver50Ms,
      );

  int _quantile(int percentile) {
    if (_samples.isEmpty) return 0;
    final List<int> sorted = List<int>.of(_samples)..sort();
    final int index = ((percentile * (sorted.length - 1) + 50) ~/ 100)
        .clamp(0, sorted.length - 1)
        .toInt();
    return sorted[index];
  }
}

final class FramePacingTracker extends ChangeNotifier {
  FramePacingTracker() {
    SchedulerBinding.instance.addTimingsCallback(_onTimings);
  }

  final FramePacingWindow _window = FramePacingWindow();
  FramePacingSnapshot _snapshot = const FramePacingSnapshot(
    p50Us: 0,
    p95Us: 0,
    p99Us: 0,
    maxUs: 0,
    spikesOver50Ms: 0,
  );

  FramePacingSnapshot get value => _snapshot;

  void _onTimings(List<FrameTiming> timings) {
    for (final FrameTiming timing in timings) {
      _window.push(timing.totalSpan.inMicroseconds);
    }
    _snapshot = _window.snapshot();
    notifyListeners();
  }

  @override
  void dispose() {
    SchedulerBinding.instance.removeTimingsCallback(_onTimings);
    super.dispose();
  }
}
