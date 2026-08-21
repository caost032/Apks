import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../../engine/game_runtime.dart';
import '../../engine/game_snapshot.dart';
import '../../native/odg_bindings.dart';
import '../design_system.dart';

final class DomainMap extends StatefulWidget {
  const DomainMap({required this.runtime, this.compact = false, super.key});
  final GameRuntime runtime;
  final bool compact;
  @override State<DomainMap> createState() => _DomainMapState();
}

final class _DomainMapState extends State<DomainMap> {
  Timer? _timer;
  double _centerX = 0, _centerZ = 0, _extent = 128000;
  double _startExtent = 128000;
  Offset _startCenter = Offset.zero;
  double? _playerX, _playerZ;
  GameMapData? _data;

  @override
  void initState() {
    super.initState();
    _refresh();
    _timer = Timer.periodic(Duration(milliseconds: widget.compact ? 900 : 550), (_) => _refresh());
  }

  @override void dispose() { _timer?.cancel(); super.dispose(); }

  void _centerPlayer() {
    final double? px = _playerX, pz = _playerZ;
    if (px == null || pz == null) return;
    setState(() { _centerX = px; _centerZ = pz; });
    _refresh();
  }

  void _refresh() {
    if (!mounted) return;
    try {
      final int res = widget.compact ? 36 : 88;
      final GameMapData next = widget.runtime.queryMap(
        minXMilli: (_centerX - _extent / 2).round(),
        minZMilli: (_centerZ - _extent / 2).round(),
        maxXMilli: (_centerX + _extent / 2).round(),
        maxZMilli: (_centerZ + _extent / 2).round(),
        width: res,
        height: res,
      );
      final GameMapMarker? player = next.markers
          .where((GameMapMarker m) => m.kind == odgMapMarkerActor && m.ownerActorId == 0)
          .firstOrNull;
      if (player != null) {
        _playerX = player.xMilli.toDouble();
        _playerZ = player.zMilli.toDouble();
        if (_data == null) { _centerX = _playerX!; _centerZ = _playerZ!; }
      }
      setState(() => _data = next);
    } catch (_) { }
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onDoubleTap: _centerPlayer,
      onScaleStart: widget.compact ? null : (ScaleStartDetails d) {
        _startExtent = _extent;
        _startCenter = Offset(_centerX, _centerZ);
      },
      onScaleUpdate: widget.compact ? null : (ScaleUpdateDetails d) {
        final Size size = context.size ?? const Size(300, 300);
        _extent = (_startExtent / d.scale).clamp(24000, 1200000).toDouble();
        _centerX = _startCenter.dx - d.focalPointDelta.dx / math.max(1.0, size.width) * _extent;
        _centerZ = _startCenter.dy - d.focalPointDelta.dy / math.max(1.0, size.height) * _extent;
        _refresh();
      },
      child: ClipRRect(
        borderRadius: BorderRadius.circular(widget.compact ? 6 : 8),
        child: DecoratedBox(
          decoration: BoxDecoration(color: const Color(0xFF0A0F13), border: Border.all(color: OdparDesign.panelEdge)),
          child: _data == null
              ? const Center(child: CircularProgressIndicator(strokeWidth: 1.5))
              : CustomPaint(painter: _MapPainter(_data!, compact: widget.compact), child: const SizedBox.expand()),
        ),
      ),
    );
  }
}

extension<T> on Iterable<T> {
  T? get firstOrNull { final Iterator<T> it = iterator; return it.moveNext() ? it.current : null; }
}

final class _MapPainter extends CustomPainter {
  _MapPainter(this.data, {required this.compact});
  final GameMapData data;
  final bool compact;

  static const List<Color> nation = <Color>[
    Color(0xFF53D9B4), Color(0xFFE06A69), Color(0xFF6E9CFF), Color(0xFFE2B85F), Color(0xFFB77DFF),
    Color(0xFF64C66E), Color(0xFFF07BB5), Color(0xFF69CEDA), Color(0xFFE89552), Color(0xFF9FA8B2),
  ];

  GameMapSample _s(int x, int y) => data.samples[y * data.width + x];
  Offset _p(int x, int y, Size size) => Offset(x * size.width / (data.width - 1), y * size.height / (data.height - 1));

  @override
  void paint(Canvas canvas, Size size) {
    if (data.width < 2 || data.height < 2 || data.samples.length < data.width * data.height) return;
    _paintRelief(canvas, size);
    if (!compact) _paintHeightContours(canvas, size);
    _paintTerritory(canvas, size);
    _paintMarkers(canvas, size);
    if (!compact) _paintCompassAndScale(canvas, size);
  }

  void _paintRelief(Canvas canvas, Size size) {
    final double cw = size.width / (data.width - 1), ch = size.height / (data.height - 1);
    final Paint p = Paint()..isAntiAlias = false;
    int minH = 1 << 30, maxH = -(1 << 30);
    for (final GameMapSample s in data.samples) { minH = math.min(minH, s.heightMilli); maxH = math.max(maxH, s.heightMilli); }
    final double range = math.max(900, maxH - minH).toDouble();
    for (int y = 0; y < data.height - 1; y += 1) {
      for (int x = 0; x < data.width - 1; x += 1) {
        final int xl = math.max(0, x - 1), xr = math.min(data.width - 1, x + 1);
        final int yu = math.max(0, y - 1), yd = math.min(data.height - 1, y + 1);
        final double dx = (_s(xr, y).heightMilli - _s(xl, y).heightMilli) / range;
        final double dz = (_s(x, yd).heightMilli - _s(x, yu).heightMilli) / range;
        final double light = (0.72 - dx * .95 + dz * .55).clamp(.42, 1.15);
        final double alt = ((_s(x, y).heightMilli - minH) / range).clamp(0.0, 1.0);
        final Color low = const Color(0xFF17231F), high = const Color(0xFF46564C);
        p.color = _shadeColor(Color.lerp(low, high, alt * .72)!, light);
        canvas.drawRect(Rect.fromLTWH(x * cw - .4, y * ch - .4, cw + 1.2, ch + 1.2), p);
      }
    }
  }

  void _paintTerritory(Canvas canvas, Size size) {
    final double cw = size.width / (data.width - 1), ch = size.height / (data.height - 1);
    final Paint fill = Paint()..isAntiAlias = false;
    final Set<int> owners = <int>{};
    for (int y = 0; y < data.height - 1; y += 1) for (int x = 0; x < data.width - 1; x += 1) {
      final int owner = _s(x, y).ownerActorPlusOne - 1;
      if (owner < 0) continue;
      owners.add(owner);
      fill.color = nation[owner % nation.length].withValues(alpha: compact ? .20 : .17);
      canvas.drawRect(Rect.fromLTWH(x * cw - .2, y * ch - .2, cw + .8, ch + .8), fill);
    }
    final Paint edge = Paint()..style = PaintingStyle.stroke..strokeWidth = compact ? 1.0 : 1.35..strokeCap = StrokeCap.round..isAntiAlias = true;
    for (final int owner in owners) {
      edge.color = nation[owner % nation.length].withValues(alpha: .90);
      _marchBinary(canvas, size, owner, edge);
    }
  }

  void _marchBinary(Canvas canvas, Size size, int owner, Paint paint) {
    for (int y = 0; y < data.height - 1; y += 1) for (int x = 0; x < data.width - 1; x += 1) {
      final bool a = _s(x, y).ownerActorPlusOne - 1 == owner;
      final bool b = _s(x + 1, y).ownerActorPlusOne - 1 == owner;
      final bool c = _s(x + 1, y + 1).ownerActorPlusOne - 1 == owner;
      final bool d = _s(x, y + 1).ownerActorPlusOne - 1 == owner;
      final int code = (a ? 8 : 0) | (b ? 4 : 0) | (c ? 2 : 0) | (d ? 1 : 0);
      if (code == 0 || code == 15) continue;
      final Offset tl = _p(x, y, size), tr = _p(x + 1, y, size), br = _p(x + 1, y + 1, size), bl = _p(x, y + 1, size);
      final Offset top = Offset((tl.dx + tr.dx) / 2, tl.dy), right = Offset(tr.dx, (tr.dy + br.dy) / 2);
      final Offset bottom = Offset((bl.dx + br.dx) / 2, bl.dy), left = Offset(tl.dx, (tl.dy + bl.dy) / 2);
      switch (code) {
        case 1:
        case 14:
          canvas.drawLine(left, bottom, paint); break;
        case 2:
        case 13:
          canvas.drawLine(bottom, right, paint); break;
        case 3:
        case 12:
          canvas.drawLine(left, right, paint); break;
        case 4:
        case 11:
          canvas.drawLine(top, right, paint); break;
        case 5:
          canvas.drawLine(top, left, paint); canvas.drawLine(bottom, right, paint); break;
        case 6:
        case 9:
          canvas.drawLine(top, bottom, paint); break;
        case 7:
        case 8:
          canvas.drawLine(top, left, paint); break;
        case 10:
          canvas.drawLine(top, right, paint); canvas.drawLine(left, bottom, paint); break;
      }
    }
  }

  void _paintHeightContours(Canvas canvas, Size size) {
    int minH = 1 << 30, maxH = -(1 << 30);
    for (final GameMapSample s in data.samples) { minH = math.min(minH, s.heightMilli); maxH = math.max(maxH, s.heightMilli); }
    const int step = 450;
    final int first = (minH ~/ step) * step;
    final Paint p = Paint()..style = PaintingStyle.stroke..strokeWidth = .55..color = const Color(0xFFB8C6C0).withValues(alpha: .17)..isAntiAlias = true;
    for (int level = first; level <= maxH; level += step) {
      for (int y = 0; y < data.height - 1; y += 1) for (int x = 0; x < data.width - 1; x += 1) {
        final int ha = _s(x, y).heightMilli, hb = _s(x + 1, y).heightMilli, hc = _s(x + 1, y + 1).heightMilli, hd = _s(x, y + 1).heightMilli;
        final List<Offset> pts = <Offset>[];
        void edge(Offset p0, Offset p1, int h0, int h1) {
          if ((h0 < level && h1 < level) || (h0 >= level && h1 >= level) || h0 == h1) return;
          final double t = ((level - h0) / (h1 - h0)).clamp(0.0, 1.0);
          pts.add(Offset.lerp(p0, p1, t)!);
        }
        final Offset tl = _p(x, y, size), tr = _p(x + 1, y, size), br = _p(x + 1, y + 1, size), bl = _p(x, y + 1, size);
        edge(tl, tr, ha, hb); edge(tr, br, hb, hc); edge(br, bl, hc, hd); edge(bl, tl, hd, ha);
        if (pts.length == 2) canvas.drawLine(pts[0], pts[1], p);
        if (pts.length == 4) { canvas.drawLine(pts[0], pts[1], p); canvas.drawLine(pts[2], pts[3], p); }
      }
    }
  }

  void _paintMarkers(Canvas canvas, Size size) {
    final Paint fill = Paint()..style = PaintingStyle.fill..isAntiAlias = true;
    final Paint line = Paint()..style = PaintingStyle.stroke..strokeWidth = 1.2..isAntiAlias = true;
    for (final GameMapMarker marker in data.markers) {
      final double nx = (marker.xMilli - data.minXMilli) / (data.maxXMilli - data.minXMilli);
      final double nz = (marker.zMilli - data.minZMilli) / (data.maxZMilli - data.minZMilli);
      if (nx < 0 || nx > 1 || nz < 0 || nz > 1) continue;
      final Offset p = Offset(nx * size.width, nz * size.height);
      if (marker.kind == odgMapMarkerActor) {
        final Color col = marker.ownerActorId == 0 ? Colors.white : nation[marker.ownerActorId % nation.length];
        fill.color = col; line.color = const Color(0xDD081015);
        final double r = marker.ownerActorId == 0 ? 5.0 : 3.4;
        canvas.drawCircle(p, r, fill); canvas.drawCircle(p, r, line);
        if (marker.ownerActorId == 0) {
          final Path north = Path()..moveTo(p.dx, p.dy - 9)..lineTo(p.dx - 3.2, p.dy - 4)..lineTo(p.dx + 3.2, p.dy - 4)..close();
          canvas.drawPath(north, fill);
        }
      } else if (marker.kind == odgMapMarkerTurret) {
        fill.color = marker.ownerActorId < nation.length ? nation[marker.ownerActorId % nation.length] : const Color(0xFFE2B85F);
        line.color = const Color(0xE6080D11);
        final Path t = Path()..moveTo(p.dx, p.dy - 5)..lineTo(p.dx - 4.5, p.dy + 4)..lineTo(p.dx + 4.5, p.dy + 4)..close();
        canvas.drawPath(t, fill); canvas.drawPath(t, line);
      } else if (marker.kind == odgMapMarkerConstruction) {
        fill.color = marker.materialTier == odgMaterialIron
            ? const Color(0xFFC9D2D7)
            : marker.materialTier == odgMaterialStone
                ? const Color(0xFF8F979A)
                : const Color(0xFFA87545);
        line.color = marker.ownerActorId < nation.length
            ? nation[marker.ownerActorId % nation.length]
            : const Color(0xE6080D11);
        final Rect r = Rect.fromCenter(center: p, width: 7.2, height: 7.2);
        canvas.drawRect(r, fill); canvas.drawRect(r, line);
      } else {
        fill.color = marker.ownerActorId < nation.length ? nation[marker.ownerActorId % nation.length].withValues(alpha: .9) : const Color(0xFFB7C2CC);
        line.color = const Color(0xE6080D11);
        final Path d = Path()..moveTo(p.dx, p.dy - 4.6)..lineTo(p.dx + 4.6, p.dy)..lineTo(p.dx, p.dy + 4.6)..lineTo(p.dx - 4.6, p.dy)..close();
        canvas.drawPath(d, fill); canvas.drawPath(d, line);
      }
    }
  }

  void _paintCompassAndScale(Canvas canvas, Size size) {
    const TextStyle label = TextStyle(color: Color(0xFFCED6D4), fontSize: 8, fontWeight: FontWeight.w700, letterSpacing: .7);
    final TextPainter north = TextPainter(text: const TextSpan(text: 'N', style: label), textDirection: TextDirection.ltr)..layout();
    north.paint(canvas, Offset(size.width - 18, 8));
    final Paint p = Paint()..color = const Color(0xFFCED6D4)..strokeWidth = 1.2;
    canvas.drawLine(Offset(size.width - 14, 23), Offset(size.width - 14, 37), p);
    canvas.drawLine(Offset(size.width - 14, 23), Offset(size.width - 18, 29), p);
    canvas.drawLine(Offset(size.width - 14, 23), Offset(size.width - 10, 29), p);

    final double worldM = (data.maxXMilli - data.minXMilli).abs() / 1000.0;
    const double px = 56;
    final double metres = worldM * px / math.max(1.0, size.width);
    final double nice = _niceDistance(metres);
    final double actualPx = nice / worldM * size.width;
    final double y = size.height - 15;
    canvas.drawLine(Offset(10, y), Offset(10 + actualPx, y), p);
    canvas.drawLine(Offset(10, y - 3), Offset(10, y + 3), p);
    canvas.drawLine(Offset(10 + actualPx, y - 3), Offset(10 + actualPx, y + 3), p);
    final String text = nice >= 1000 ? '${(nice / 1000).toStringAsFixed(nice >= 10000 ? 0 : 1)} km' : '${nice.toStringAsFixed(0)} m';
    final TextPainter tp = TextPainter(text: TextSpan(text: text, style: label), textDirection: TextDirection.ltr)..layout();
    tp.paint(canvas, Offset(10, y - 13));
  }

  static double _niceDistance(double target) {
    if (target <= 0) return 1;
    final double pow10 = math.pow(10, (math.log(target) / math.ln10).floor()).toDouble();
    final double n = target / pow10;
    final double step = n < 2 ? 1 : n < 5 ? 2 : 5;
    return step * pow10;
  }

  static Color _shadeColor(Color base, double light) {
    if (light <= 1.0) return Color.lerp(const Color(0xFF05090C), base, light.clamp(0.0, 1.0))!;
    return Color.lerp(base, const Color(0xFFD6E1DC), ((light - 1.0) * .34).clamp(0.0, .18))!;
  }

  @override bool shouldRepaint(_MapPainter oldDelegate) => oldDelegate.data != data || oldDelegate.compact != compact;
}
