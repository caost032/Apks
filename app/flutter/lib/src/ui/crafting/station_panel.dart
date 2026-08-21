import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../../engine/game_runtime.dart';
import '../../engine/game_snapshot.dart';
import '../../native/odg_bindings.dart';
import '../design_system.dart';
import '../item_glyph.dart';

final class StationPanel extends StatefulWidget {
  const StationPanel({required this.runtime, required this.artifact, required this.onClose, super.key});
  final GameRuntime runtime;
  final GameArtifact artifact;
  final VoidCallback onClose;

  @override
  State<StationPanel> createState() => _StationPanelState();
}

final class _StationPanelState extends State<StationPanel> {
  int? _selectedRecipe;
  int _quantity = 1;

  @override
  Widget build(BuildContext context) {
    final bool chest = widget.artifact.itemType == odgItemChest;
    return ColoredBox(
      color: const Color(0x33000000),
      child: SafeArea(
        child: Align(
          alignment: Alignment.centerRight,
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 430),
            child: DecoratedBox(
              decoration: const BoxDecoration(color: Color(0xF60A0E12), border: Border(left: BorderSide(color: OdparDesign.panelEdge))),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: <Widget>[
                  _Header(title: widget.artifact.name.toUpperCase(), onClose: widget.onClose),
                  const Divider(height: 1, color: OdparDesign.panelEdge),
                  Expanded(child: chest ? _storage() : _crafting()),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _crafting() {
    final List<GameRecipe> recipes = widget.runtime.recipesForStation(widget.artifact.itemType);
    GameRecipe? selected;
    for (final GameRecipe recipe in recipes) {
      if (recipe.recipeId == _selectedRecipe) selected = recipe;
    }
    selected ??= recipes.isEmpty ? null : recipes.first;
    if (selected != null && _selectedRecipe == null) _selectedRecipe = selected.recipeId;
    final GameRepairQuote? repair = widget.runtime.repairQuote();
    final int maxCraft = selected?.maxCraftable ?? 0;
    final int qty = _quantity.clamp(1, maxCraft > 0 ? maxCraft : 1).toInt();
    return Column(
      children: <Widget>[
        Expanded(
          child: ListView.separated(
            padding: const EdgeInsets.all(14),
            itemCount: recipes.length,
            separatorBuilder: (_, __) => const SizedBox(height: 6),
            itemBuilder: (BuildContext context, int index) {
              final GameRecipe recipe = recipes[index];
              final bool active = recipe.recipeId == selected?.recipeId;
              return InkWell(
                onTap: () => setState(() { _selectedRecipe = recipe.recipeId; _quantity = 1; }),
                child: Container(
                  padding: const EdgeInsets.all(11),
                  decoration: BoxDecoration(
                    color: active ? const Color(0xFF16231F) : const Color(0xFF10161B),
                    border: Border.all(color: active ? OdparDesign.accent : OdparDesign.panelEdge),
                    borderRadius: BorderRadius.circular(6),
                  ),
                  child: Row(
                    children: <Widget>[
                      OdparItemGlyph(typeId:recipe.outputItemType,materialTier:recipe.outputMaterialTier,size:27,emphasized:active),
                      const SizedBox(width:10),
                      Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: <Widget>[
                        Text(_recipeName(recipe), style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 12)),
                        const SizedBox(height: 5),
                        Text(recipe.ingredients.map((GameRecipeIngredient e) => '${e.quantity}× ${e.label}').join('  ·  '), style: const TextStyle(color: OdparDesign.textMuted, fontSize: 9)),
                      ])),
                      Text('MAX ${recipe.maxCraftable}', style: TextStyle(color: recipe.maxCraftable > 0 ? OdparDesign.accent : OdparDesign.textMuted, fontSize: 9)),
                    ],
                  ),
                ),
              );
            },
          ),
        ),
        if (repair != null && repair.durabilityAfter > repair.durabilityBefore)
          Padding(
            padding: const EdgeInsets.fromLTRB(14, 0, 14, 8),
            child: OutlinedButton.icon(
              onPressed: widget.runtime.repairSelected,
              icon: const Icon(Icons.build_outlined, size: 16),
              label: Text('REPARAR · ${repair.costQuantity}× ${gameItemName(repair.costItemType)}'),
            ),
          ),
        if (selected != null)
          Container(
            padding: const EdgeInsets.all(14),
            decoration: const BoxDecoration(border: Border(top: BorderSide(color: OdparDesign.panelEdge))),
            child: Row(children: <Widget>[
              IconButton(onPressed: qty > 1 ? () => setState(() => _quantity = qty - 1) : null, icon: const Icon(Icons.remove)),
              SizedBox(width: 42, child: Text('$qty', textAlign: TextAlign.center, style: const TextStyle(fontFeatures: <ui.FontFeature>[ui.FontFeature.tabularFigures()]))),
              IconButton(onPressed: qty < maxCraft ? () => setState(() => _quantity = qty + 1) : null, icon: const Icon(Icons.add)),
              TextButton(onPressed: maxCraft > 0 ? () => setState(() => _quantity = maxCraft) : null, child: const Text('MAX')),
              const Spacer(),
              FilledButton(onPressed: maxCraft > 0 ? () { widget.runtime.craft(selected!.recipeId, qty); setState(() {}); } : null, child: const Text('CREAR')),
            ]),
          ),
      ],
    );
  }

  Widget _storage() {
    final GameStorage? storage = widget.runtime.openedStorage();
    if (storage == null) return const Center(child: Text('ALMACENAMIENTO NO DISPONIBLE', style: TextStyle(color: OdparDesign.textMuted)));
    final GameInventory inventory = widget.runtime.snapshot.inventory;
    return Row(children: <Widget>[
      Expanded(child: _slotGrid(
        title: 'INVENTARIO', slots: inventory.slots, count: inventory.slotCount,
        onTap: (int slot) => widget.runtime.storageDeposit(storage.artifactId, slot),
      )),
      const VerticalDivider(width: 1, color: OdparDesign.panelEdge),
      Expanded(child: _slotGrid(
        title: 'COFRE · ${storage.usedSlots}/${storage.slotCount}', slots: storage.slots, count: storage.slotCount,
        onTap: (int slot) => widget.runtime.storageWithdraw(storage.artifactId, slot),
      )),
    ]);
  }

  Widget _slotGrid({required String title, required List<GameItemStack> slots, required int count, required ValueChanged<int> onTap}) {
    return Column(children: <Widget>[
      Padding(padding: const EdgeInsets.all(10), child: Text(title, style: const TextStyle(fontSize: 9, color: OdparDesign.textMuted, letterSpacing: 1))),
      Expanded(child: GridView.builder(
        padding: const EdgeInsets.all(8),
        gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(crossAxisCount: 3, crossAxisSpacing: 5, mainAxisSpacing: 5),
        itemCount: count,
        itemBuilder: (BuildContext context, int index) {
          final GameItemStack stack = index < slots.length ? slots[index] : const GameItemStack.empty();
          return InkWell(
            onTap: stack.isEmpty ? null : () { onTap(index); setState(() {}); },
            child: Container(
              padding: const EdgeInsets.all(6),
              decoration: BoxDecoration(color: const Color(0xFF11161C), border: Border.all(color: OdparDesign.panelEdge), borderRadius: BorderRadius.circular(4)),
              child: Stack(children:<Widget>[
                Center(child:stack.isEmpty?const Text('—',style:TextStyle(color:Color(0xFF46515B))):OdparItemGlyph(typeId:stack.typeId,materialTier:stack.materialTier,size:24)),
                if(!stack.isEmpty&&stack.quantity>1)Positioned(right:1,bottom:0,child:Text('×${stack.quantity}',style:const TextStyle(fontSize:7,fontWeight:FontWeight.w700,color:OdparDesign.text))),
              ]),
            ),
          );
        },
      )),
    ]);
  }

  String _recipeName(GameRecipe recipe) => '${gameItemName(recipe.outputItemType)}${recipe.outputMaterialTier == odgMaterialNone || !gameItemShowsMaterial(recipe.outputItemType) ? '' : ' · ${gameMaterialName(recipe.outputMaterialTier)}'} ×${recipe.outputQuantity}';
}

final class _Header extends StatelessWidget {
  const _Header({required this.title, required this.onClose});
  final String title; final VoidCallback onClose;
  @override Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(16, 12, 8, 10),
    child: Row(children: <Widget>[
      Expanded(child: Text(title, style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w700, letterSpacing: 1.2))),
      IconButton(onPressed: onClose, icon: const Icon(Icons.close_rounded)),
    ]),
  );
}
