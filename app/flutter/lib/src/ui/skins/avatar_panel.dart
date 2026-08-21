import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../../engine/game_runtime.dart';
import '../../native/odg_bindings.dart';
import '../../skins/avatar_skin_service.dart';
import '../design_system.dart';

final class AvatarPanel extends StatefulWidget {
  const AvatarPanel({required this.runtime, required this.skins, super.key});
  final GameRuntime runtime;
  final AvatarSkinService skins;
  @override State<AvatarPanel> createState()=>_AvatarPanelState();
}

final class _AvatarPanelState extends State<AvatarPanel> {
  int _face=odgAvatarFaceFront;
  int _yaw=7000;
  ui.Image? _preview;
  bool _busy=false;
  double _dragRemainder=0;

  @override void initState(){super.initState();_refresh();}
  @override void dispose(){_preview?.dispose();super.dispose();}

  Future<void> _refresh() async {
    if(_busy||!mounted)return;_busy=true;
    try{
      final ui.Image? next=await widget.runtime.renderAvatarPreview(_yaw);
      if(!mounted){next?.dispose();return;}
      final ui.Image? old=_preview;setState(()=>_preview=next);old?.dispose();
    }finally{_busy=false;}
  }

  void _rotate(double dx){
    _dragRemainder+=dx*155;
    final int delta=_dragRemainder.truncate();_dragRemainder-=delta;
    if(delta!=0){_yaw=(_yaw+delta)&0xffff;_refresh();}
  }

  Future<void> _load() async {
    if(await widget.skins.pickAndApply(_face))await _refresh();
  }

  @override Widget build(BuildContext context)=>ListView(
    padding:const EdgeInsets.all(14),children:<Widget>[
      AspectRatio(aspectRatio:1.35,child:GestureDetector(
        onHorizontalDragUpdate:(DragUpdateDetails d)=>_rotate(d.delta.dx),
        child:DecoratedBox(
          decoration:BoxDecoration(color:const Color(0xFF0C1116),border:Border.all(color:OdparDesign.panelEdge),borderRadius:BorderRadius.circular(7)),
          child:_preview==null?const Center(child:CircularProgressIndicator(strokeWidth:1.5)):RawImage(image:_preview,fit:BoxFit.contain,filterQuality:FilterQuality.medium),
        ),
      )),
      const SizedBox(height:10),
      const Text('ARRASTRA PARA ROTAR · PREVIEW 3D RENDERIZADA EN C',style:TextStyle(fontSize:8,color:OdparDesign.textMuted,letterSpacing:0.8),textAlign:TextAlign.center),
      const SizedBox(height:14),
      Wrap(spacing:6,runSpacing:6,children:<Widget>[
        for(final (int,String) f in const <(int,String)>[(0,'FRENTE'),(1,'DERECHA'),(2,'ATRÁS'),(3,'IZQUIERDA'),(4,'ARRIBA'),(5,'ABAJO')])
          ChoiceChip(label:Text(f.$2,style:const TextStyle(fontSize:8)),selected:_face==f.$1,onSelected:(_)=>setState(()=>_face=f.$1)),
      ]),
      const SizedBox(height:14),
      FilledButton.icon(onPressed:_load,icon:const Icon(Icons.photo_library_outlined,size:17),label:const Text('CARGAR TEXTURA')),
      const SizedBox(height:8),
      const Text('Las imágenes se normalizan a 256×256 RGBA8 con cover + center crop. La copia normalizada se conserva localmente; el renderer nunca retiene punteros Dart.',style:TextStyle(fontSize:9,color:OdparDesign.textMuted,height:1.4)),
    ],
  );
}
