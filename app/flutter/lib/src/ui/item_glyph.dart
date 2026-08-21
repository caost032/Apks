import 'package:flutter/material.dart';

import '../native/odg_bindings.dart';
import 'design_system.dart';

/// Compact vector icon language shared by hotbar, inventory and crafting.
/// It intentionally describes item *capabilities* rather than using letters, so the UI
/// stays readable at phone scale and does not regress to MAD/PIE/TOR debug abbreviations.
final class OdparItemGlyph extends StatelessWidget {
  const OdparItemGlyph({
    required this.typeId,
    required this.materialTier,
    this.size = 24,
    this.emphasized = false,
    super.key,
  });

  final int typeId;
  final int materialTier;
  final double size;
  final bool emphasized;

  @override
  Widget build(BuildContext context) => SizedBox.square(
        dimension: size,
        child: CustomPaint(
          painter: _ItemGlyphPainter(typeId: typeId, materialTier: materialTier, emphasized: emphasized),
        ),
      );
}

final class _ItemGlyphPainter extends CustomPainter {
  const _ItemGlyphPainter({required this.typeId, required this.materialTier, required this.emphasized});
  final int typeId;
  final int materialTier;
  final bool emphasized;

  Color get _tier => switch (materialTier) {
        odgMaterialWood => const Color(0xFFB4936C),
        odgMaterialStone => const Color(0xFFABB3B8),
        odgMaterialIron => const Color(0xFFD5E0E3),
        _ => OdparDesign.text,
      };

  @override
  void paint(Canvas canvas, Size size) {
    final double s = size.shortestSide;
    final Paint stroke = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = (s * .075).clamp(1.2, 2.2)
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round
      ..color = emphasized ? OdparDesign.accent : _tier;
    final Paint fill = Paint()..color = stroke.color.withValues(alpha: .20);
    final Offset c = Offset(size.width / 2, size.height / 2);
    final Rect box = Rect.fromCenter(center: c, width: s * .74, height: s * .74);

    switch (typeId) {
      case odgItemWood:
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: c, width: s * .68, height: s * .30), Radius.circular(s * .12)), fill);
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: c, width: s * .68, height: s * .30), Radius.circular(s * .12)), stroke);
        canvas.drawCircle(Offset(c.dx + s * .25, c.dy), s * .08, stroke);
        canvas.drawLine(Offset(c.dx - s * .18, c.dy - s * .12), Offset(c.dx - s * .04, c.dy + s * .12), stroke);
        return;
      case odgItemStone:
        final Path p = Path()
          ..moveTo(c.dx - s * .32, c.dy + s * .18)
          ..lineTo(c.dx - s * .22, c.dy - s * .18)
          ..lineTo(c.dx + s * .03, c.dy - s * .30)
          ..lineTo(c.dx + s * .30, c.dy - s * .08)
          ..lineTo(c.dx + s * .24, c.dy + s * .23)
          ..close();
        canvas.drawPath(p, fill); canvas.drawPath(p, stroke);
        canvas.drawLine(Offset(c.dx - s * .17, c.dy - s * .10), Offset(c.dx + s * .18, c.dy + s * .12), stroke);
        return;
      case odgItemIron:
        final RRect ingot = RRect.fromRectAndRadius(Rect.fromCenter(center: c, width: s * .68, height: s * .34), Radius.circular(s * .06));
        canvas.drawRRect(ingot, fill); canvas.drawRRect(ingot, stroke);
        canvas.drawLine(Offset(c.dx - s * .22, c.dy - s * .05), Offset(c.dx + s * .22, c.dy - s * .05), stroke);
        return;
      case odgItemAmmo:
        for (int i = -1; i <= 1; i += 1) {
          final double x = c.dx + i * s * .18;
          canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(x, c.dy + s * .04), width: s * .13, height: s * .48), Radius.circular(s * .045)), stroke);
          canvas.drawLine(Offset(x - s * .055, c.dy - s * .18), Offset(x, c.dy - s * .28), stroke);
          canvas.drawLine(Offset(x + s * .055, c.dy - s * .18), Offset(x, c.dy - s * .28), stroke);
        }
        return;
      case odgItemReprogramChip:
        canvas.drawRRect(RRect.fromRectAndRadius(box, Radius.circular(s * .08)), fill); canvas.drawRRect(RRect.fromRectAndRadius(box, Radius.circular(s * .08)), stroke);
        canvas.drawCircle(c, s * .09, stroke);
        for (final Offset d in const <Offset>[Offset(-1,0),Offset(1,0),Offset(0,-1),Offset(0,1)]) {
          final Offset end = c + Offset(d.dx * s * .27, d.dy * s * .27);
          canvas.drawLine(c + Offset(d.dx * s * .09, d.dy * s * .09), end, stroke); canvas.drawCircle(end, s * .035, stroke);
        }
        return;
      case odgItemAscensionChip:
        canvas.drawRRect(RRect.fromRectAndRadius(box, Radius.circular(s * .08)), fill); canvas.drawRRect(RRect.fromRectAndRadius(box, Radius.circular(s * .08)), stroke);
        final Path arrow = Path()..moveTo(c.dx, c.dy - s*.28)..lineTo(c.dx-s*.18,c.dy-s*.04)..lineTo(c.dx-s*.07,c.dy-s*.04)..lineTo(c.dx-s*.07,c.dy+s*.25)..lineTo(c.dx+s*.07,c.dy+s*.25)..lineTo(c.dx+s*.07,c.dy-s*.04)..lineTo(c.dx+s*.18,c.dy-s*.04)..close();
        canvas.drawPath(arrow, stroke);
        return;
      case odgItemAxe:
        canvas.drawLine(Offset(c.dx - s*.18,c.dy+s*.30), Offset(c.dx+s*.16,c.dy-s*.27), stroke);
        final Path blade=Path()..moveTo(c.dx+s*.10,c.dy-s*.24)..quadraticBezierTo(c.dx+s*.34,c.dy-s*.20,c.dx+s*.30,c.dy+s*.02)..lineTo(c.dx+s*.04,c.dy-s*.03)..close();
        canvas.drawPath(blade, fill); canvas.drawPath(blade, stroke);
        return;
      case odgItemPickaxe:
        canvas.drawLine(Offset(c.dx, c.dy+s*.30), Offset(c.dx, c.dy-s*.18), stroke);
        final Path head=Path()..moveTo(c.dx-s*.31,c.dy-s*.08)..quadraticBezierTo(c.dx,c.dy-s*.31,c.dx+s*.31,c.dy-s*.08);
        canvas.drawPath(head, stroke);
        return;
      case odgItemTurret:
        canvas.drawLine(Offset(c.dx-s*.27,c.dy+s*.27), Offset(c.dx-s*.12,c.dy+s*.05), stroke);
        canvas.drawLine(Offset(c.dx+s*.27,c.dy+s*.27), Offset(c.dx+s*.12,c.dy+s*.05), stroke);
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(c.dx,c.dy+s*.02), width:s*.32,height:s*.28),Radius.circular(s*.05)),fill);
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(c.dx,c.dy+s*.02), width:s*.32,height:s*.28),Radius.circular(s*.05)),stroke);
        canvas.drawLine(Offset(c.dx-s*.20,c.dy-s*.13), Offset(c.dx+s*.18,c.dy-s*.13), stroke);
        canvas.drawLine(Offset(c.dx+s*.12,c.dy-s*.13), Offset(c.dx+s*.34,c.dy-s*.27), stroke);
        return;
      case odgItemWorkbench:
        canvas.drawLine(Offset(c.dx-s*.29,c.dy+s*.27), Offset(c.dx-s*.22,c.dy-s*.02), stroke);
        canvas.drawLine(Offset(c.dx+s*.29,c.dy+s*.27), Offset(c.dx+s*.22,c.dy-s*.02), stroke);
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(c.dx,c.dy-s*.10), width:s*.70,height:s*.20),Radius.circular(s*.04)),fill);
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(c.dx,c.dy-s*.10), width:s*.70,height:s*.20),Radius.circular(s*.04)),stroke);
        return;
      case odgItemSmithy:
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(c.dx-s*.05,c.dy+s*.08), width:s*.58,height:s*.48),Radius.circular(s*.04)),fill);
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(c.dx-s*.05,c.dy+s*.08), width:s*.58,height:s*.48),Radius.circular(s*.04)),stroke);
        canvas.drawRect(Rect.fromCenter(center: Offset(c.dx+s*.22,c.dy-s*.20), width:s*.16,height:s*.38), stroke);
        canvas.drawLine(Offset(c.dx-s*.23,c.dy+s*.03), Offset(c.dx+s*.05,c.dy+s*.03), stroke);
        return;
      case odgItemChest:
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: c, width:s*.70,height:s*.48),Radius.circular(s*.07)),fill);
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: c, width:s*.70,height:s*.48),Radius.circular(s*.07)),stroke);
        canvas.drawLine(Offset(c.dx-s*.34,c.dy-s*.05), Offset(c.dx+s*.34,c.dy-s*.05), stroke);
        canvas.drawRect(Rect.fromCenter(center: Offset(c.dx,c.dy+s*.04),width:s*.10,height:s*.13),stroke);
        return;
      case odgItemBackpack:
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(c.dx,c.dy+s*.05),width:s*.58,height:s*.58),Radius.circular(s*.12)),fill);
        canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(c.dx,c.dy+s*.05),width:s*.58,height:s*.58),Radius.circular(s*.12)),stroke);
        canvas.drawArc(Rect.fromCenter(center: Offset(c.dx,c.dy-s*.20),width:s*.28,height:s*.28),3.3,2.7,false,stroke);
        canvas.drawLine(Offset(c.dx-s*.20,c.dy+s*.02),Offset(c.dx+s*.20,c.dy+s*.02),stroke);
        return;
      default:
        canvas.drawCircle(c, s * .24, stroke);
        return;
    }
  }

  @override
  bool shouldRepaint(_ItemGlyphPainter oldDelegate) => oldDelegate.typeId != typeId || oldDelegate.materialTier != materialTier || oldDelegate.emphasized != emphasized;
}
