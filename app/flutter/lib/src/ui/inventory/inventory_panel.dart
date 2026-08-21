import 'package:flutter/material.dart';

import '../../engine/game_runtime.dart';
import '../../engine/game_snapshot.dart';
import '../../native/odg_bindings.dart';
import '../../skins/avatar_skin_service.dart';
import '../artifacts/artifacts_panel.dart';
import '../design_system.dart';
import '../item_glyph.dart';
import '../map/domain_map.dart';
import '../skins/avatar_panel.dart';

enum _InventoryTabKind { objects, character, domain, artifacts }

final class InventoryPanel extends StatefulWidget {
  const InventoryPanel({required this.runtime, required this.skins, required this.onClose, super.key});
  final GameRuntime runtime;
  final AvatarSkinService skins;
  final VoidCallback onClose;
  @override State<InventoryPanel> createState()=>_InventoryPanelState();
}

final class _InventoryPanelState extends State<InventoryPanel> {
  _InventoryTabKind _tab=_InventoryTabKind.objects;

  @override Widget build(BuildContext context){
    final GameInventory inventory=widget.runtime.snapshot.inventory;
    return ColoredBox(color:const Color(0x4A000000),child:SafeArea(child:LayoutBuilder(builder:(BuildContext context,BoxConstraints constraints){
      final bool landscape=constraints.maxWidth>constraints.maxHeight;
      final double width=landscape?500:(constraints.maxWidth*0.90).clamp(300.0,500.0).toDouble();
      return Align(alignment:Alignment.centerRight,child:SizedBox(width:width,height:double.infinity,child:DecoratedBox(
        decoration:const BoxDecoration(color:Color(0xF20B0F13),border:Border(left:BorderSide(color:OdparDesign.panelEdge))),
        child:Column(crossAxisAlignment:CrossAxisAlignment.stretch,children:<Widget>[
          Padding(padding:const EdgeInsets.fromLTRB(18,14,8,10),child:Row(children:<Widget>[
            const Expanded(child:Column(crossAxisAlignment:CrossAxisAlignment.start,children:<Widget>[
              Text('INVENTARIO',style:TextStyle(color:OdparDesign.text,fontSize:17,fontWeight:FontWeight.w600,letterSpacing:1)),
              SizedBox(height:3),Text('EL MUNDO SIGUE EN CURSO',style:TextStyle(color:OdparDesign.textMuted,fontSize:8,fontWeight:FontWeight.w600,letterSpacing:1.4)),
            ])),
            IconButton(tooltip:'Cerrar',onPressed:widget.onClose,icon:const Icon(Icons.close_rounded),color:OdparDesign.textMuted),
          ])),
          const Divider(height:1,color:OdparDesign.panelEdge),
          SingleChildScrollView(scrollDirection:Axis.horizontal,padding:const EdgeInsets.symmetric(horizontal:12,vertical:8),child:Row(children:<Widget>[
            _tabButton(_InventoryTabKind.objects,'OBJETOS'),_tabButton(_InventoryTabKind.character,'PERSONAJE'),_tabButton(_InventoryTabKind.domain,'DOMINIO'),_tabButton(_InventoryTabKind.artifacts,'ARTEFACTOS'),
          ])),
          const Divider(height:1,color:OdparDesign.panelEdge),
          Expanded(child:switch(_tab){
            _InventoryTabKind.objects=>_objects(inventory,landscape),
            _InventoryTabKind.character=>AvatarPanel(runtime:widget.runtime,skins:widget.skins),
            _InventoryTabKind.domain=>_domain(),
            _InventoryTabKind.artifacts=>ArtifactsPanel(runtime:widget.runtime),
          }),
        ]),
      )));
    })));
  }

  Widget _tabButton(_InventoryTabKind value,String label)=>Padding(padding:const EdgeInsets.only(right:4),child:TextButton(
    style:TextButton.styleFrom(foregroundColor:_tab==value?OdparDesign.accent:OdparDesign.textMuted,padding:const EdgeInsets.symmetric(horizontal:9,vertical:5),minimumSize:Size.zero),
    onPressed:()=>setState(()=>_tab=value),child:Text(label,style:const TextStyle(fontSize:8,fontWeight:FontWeight.w700,letterSpacing:.9)),
  ));

  Widget _objects(GameInventory inventory,bool landscape){
    final GameItemStack selected=inventory.selectedSlot<inventory.slots.length?inventory.slots[inventory.selectedSlot]:const GameItemStack.empty();
    final int capabilities=selected.isEmpty?0:widget.runtime.itemCapabilityBits(selected.typeId);
    final bool canPlace=(capabilities&odgItemCapPlace)!=0;
    final bool canConsume=(capabilities&odgItemCapConsume)!=0;
    final bool canPlant=(capabilities&odgItemCapPlant)!=0;
    final bool canDrink=(capabilities&odgItemCapDrink)!=0&&selected.fluidUnits>0;
    final GameRepairQuote? repair=widget.runtime.repairQuote();
    return Column(children:<Widget>[
      Expanded(child:GridView.builder(
        padding:const EdgeInsets.all(14),gridDelegate:SliverGridDelegateWithFixedCrossAxisCount(crossAxisCount:landscape?5:3,crossAxisSpacing:7,mainAxisSpacing:7,childAspectRatio:1.04),itemCount:inventory.slotCount,
        itemBuilder:(BuildContext context,int index){
          final GameItemStack stack=index<inventory.slots.length?inventory.slots[index]:const GameItemStack.empty();
          return InkWell(onTap:(){widget.runtime.selectSlot(index);setState((){});},borderRadius:BorderRadius.circular(5),child:Container(
            padding:const EdgeInsets.all(8),decoration:BoxDecoration(color:index==inventory.selectedSlot?const Color(0xFF17231F):const Color(0xFF11161C),borderRadius:BorderRadius.circular(5),border:Border.all(color:index==inventory.selectedSlot?OdparDesign.accent:OdparDesign.panelEdge)),
            child:Column(crossAxisAlignment:CrossAxisAlignment.start,children:<Widget>[
              Row(children:<Widget>[
                Text('${index+1}'.padLeft(2,'0'),style:const TextStyle(color:OdparDesign.textMuted,fontSize:8)),
                const Spacer(),
                if(!stack.isEmpty) Text(stack.materialName.toUpperCase(),style:const TextStyle(color:OdparDesign.textMuted,fontSize:6.5,letterSpacing:.7)),
              ]),
              const Spacer(),
              Center(child:stack.isEmpty
                ? const Text('—',style:TextStyle(color:Color(0xFF46515B),fontSize:15))
                : OdparItemGlyph(typeId:stack.typeId,materialTier:stack.materialTier,size:30)),
              const Spacer(),
              Text(stack.isEmpty?'VACÍO':stack.itemName.toUpperCase(),maxLines:1,overflow:TextOverflow.ellipsis,style:TextStyle(color:stack.isEmpty?const Color(0xFF46515B):OdparDesign.text,fontSize:7.5,fontWeight:FontWeight.w700,letterSpacing:.3)),
              if(!stack.isEmpty&&stack.quantity>1)Text('×${stack.quantity}',style:const TextStyle(color:OdparDesign.textMuted,fontSize:7,fontWeight:FontWeight.w700)),
              if(!stack.isEmpty&&stack.maxDurability>0)...<Widget>[const SizedBox(height:5),LinearProgressIndicator(value:stack.durability/stack.maxDurability,minHeight:2,color:OdparDesign.accent,backgroundColor:const Color(0xFF29313A))],
            ]),
          ));
        },
      )),
      Container(padding:const EdgeInsets.all(14),decoration:const BoxDecoration(border:Border(top:BorderSide(color:OdparDesign.panelEdge))),child:Row(children:<Widget>[
        Expanded(child:Column(crossAxisAlignment:CrossAxisAlignment.start,children:<Widget>[
          Text(selected.isEmpty?'SLOT VACÍO':selected.displayLabel,style:const TextStyle(fontWeight:FontWeight.w700,fontSize:11)),
          if(!selected.isEmpty)Text(selected.maxDurability>0?'Durabilidad ${selected.durability}/${selected.maxDurability}':'Cantidad ${selected.quantity}',style:const TextStyle(fontSize:8,color:OdparDesign.textMuted)),
        ])),
        if(canPlace)OutlinedButton(onPressed:(){widget.runtime.placeSelected();widget.onClose();},child:const Text('COLOCAR')),
        if(canConsume)OutlinedButton(onPressed:(){widget.runtime.consumeSelected();setState((){});},child:const Text('COMER')),
        if(canPlant)OutlinedButton(onPressed:(){widget.runtime.plantSelected();widget.onClose();},child:const Text('PLANTAR')),
        if(canDrink)OutlinedButton(onPressed:(){widget.runtime.drinkSelected();setState((){});},child:const Text('BEBER')),
        if(selected.typeId==odgItemBackpack)OutlinedButton(onPressed:(){widget.runtime.equipBackpack();setState((){});},child:const Text('EQUIPAR')),
        if(repair!=null&&repair.durabilityAfter>repair.durabilityBefore)OutlinedButton(onPressed:(){widget.runtime.repairSelected();setState((){});},child:const Text('REPARAR')),
        if(!selected.isEmpty)OutlinedButton(onPressed:(){widget.runtime.dropSelected();setState((){});},child:const Text('SOLTAR')),
      ])),
    ]);
  }

  Widget _domain()=>Padding(padding:const EdgeInsets.all(14),child:Column(crossAxisAlignment:CrossAxisAlignment.stretch,children:<Widget>[
    Row(children:<Widget>[
      Expanded(child:_metric('ÁREA','${widget.runtime.snapshot.territoryTotalCells} m²')),
      const SizedBox(width:8),Expanded(child:_metric('CUOTA','${widget.runtime.snapshot.territoryPercent.toStringAsFixed(1)}%')),
      const SizedBox(width:8),Expanded(child:_metric('ARTEFACTOS','${widget.runtime.snapshot.artifacts.where((GameArtifact a)=>a.ownerActorId==0).length}')),
    ]),
    const SizedBox(height:10),Expanded(child:DomainMap(runtime:widget.runtime)),
    const SizedBox(height:6),const Text('ARRASTRA / PINCH · DOBLE TOQUE PARA CENTRAR',textAlign:TextAlign.center,style:TextStyle(fontSize:8,color:OdparDesign.textMuted,letterSpacing:.8)),
  ]));

  Widget _metric(String label,String value)=>Container(padding:const EdgeInsets.all(9),decoration:BoxDecoration(color:const Color(0xFF10161B),border:Border.all(color:OdparDesign.panelEdge),borderRadius:BorderRadius.circular(5)),child:Column(crossAxisAlignment:CrossAxisAlignment.start,children:<Widget>[
    Text(label,style:const TextStyle(fontSize:7,color:OdparDesign.textMuted,letterSpacing:1)),const SizedBox(height:3),Text(value,style:const TextStyle(fontSize:12,fontWeight:FontWeight.w700)),
  ]));
}
