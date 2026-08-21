import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../../engine/game_snapshot.dart';
import '../../native/odg_bindings.dart';
import '../design_system.dart';
import '../item_glyph.dart';

final class GameHotbar extends StatelessWidget {
  const GameHotbar({
    required this.inventory,
    required this.onSelect,
    required this.onInventory,
    super.key,
  });

  final GameInventory inventory;
  final ValueChanged<int> onSelect;
  final VoidCallback onInventory;

  @override
  Widget build(BuildContext context) {
    return DecoratedBox(
      decoration: BoxDecoration(
        color: const Color(0xB80A0F14),
        border: Border.all(color: const Color(0x85505C66)),
        borderRadius: BorderRadius.circular(7),
      ),
      child: Padding(
        padding: const EdgeInsets.all(5),
        child: Row(
          children: <Widget>[
            for (int slot = 0; slot < odgInventoryBaseSlots; slot += 1) ...<Widget>[
              Expanded(
                child: _HotbarSlot(
                  index: slot,
                  stack: slot < inventory.slots.length
                      ? inventory.slots[slot]
                      : const GameItemStack.empty(),
                  selected: slot == inventory.selectedSlot,
                  onTap: () => onSelect(slot),
                ),
              ),
              if (slot + 1 < odgInventoryBaseSlots) const SizedBox(width: 4),
            ],
            const SizedBox(width: 6),
            SizedBox(
              width: 42,
              height: double.infinity,
              child: IconButton(
                tooltip: 'Inventario',
                padding: EdgeInsets.zero,
                style: IconButton.styleFrom(
                  foregroundColor: OdparDesign.textMuted,
                  side: const BorderSide(color: OdparDesign.panelEdge),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(5),
                  ),
                ),
                onPressed: onInventory,
                icon: const Icon(Icons.inventory_2_outlined, size: 17),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

final class _HotbarSlot extends StatelessWidget {
  const _HotbarSlot({
    required this.index,
    required this.stack,
    required this.selected,
    required this.onTap,
  });

  final int index;
  final GameItemStack stack;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(4),
      child: DecoratedBox(
        decoration: BoxDecoration(
          color: selected ? const Color(0xC2182724) : const Color(0xA80E1419),
          borderRadius: BorderRadius.circular(4),
          border: Border.all(
            color: selected ? OdparDesign.accent : OdparDesign.panelEdge,
            width: selected ? 1.4 : 1,
          ),
        ),
        child: Stack(
          children: <Widget>[
            Center(
              child: stack.isEmpty
                  ? const Text('—', style: TextStyle(color: Color(0xFF46515B), fontSize: 10))
                  : OdparItemGlyph(
                      typeId: stack.typeId,
                      materialTier: stack.materialTier,
                      size: 23,
                      emphasized: selected,
                    ),
            ),
            Positioned(
              left: 4,
              top: 3,
              child: Text(
                '${index + 1}',
                style: const TextStyle(
                  color: OdparDesign.textMuted,
                  fontSize: 7,
                  fontFeatures: <ui.FontFeature>[ui.FontFeature.tabularFigures()],
                ),
              ),
            ),
            if (!stack.isEmpty && stack.quantity > 1)
              Positioned(
                right: 4,
                bottom: 3,
                child: Text(
                  '${stack.quantity}',
                  style: const TextStyle(
                    color: OdparDesign.text,
                    fontSize: 8,
                    fontWeight: FontWeight.w700,
                    fontFeatures: <ui.FontFeature>[
                      ui.FontFeature.tabularFigures(),
                    ],
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}
