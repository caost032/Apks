import 'package:flutter/material.dart';

import '../../profile/game_profile.dart';
import '../design_system.dart';

final class ControlEditor extends StatefulWidget {
  const ControlEditor({required this.profile, required this.onChanged, required this.onClose, super.key});
  final ControlProfile profile;
  final ValueChanged<ControlProfile> onChanged;
  final VoidCallback onClose;
  @override State<ControlEditor> createState()=>_ControlEditorState();
}

final class _ControlEditorState extends State<ControlEditor>{
  late ControlProfile _profile;
  String _selected='joystick';
  @override void initState(){super.initState();_profile=widget.profile;}
  ControlAnchor get _anchor=>switch(_selected){'interact'=>_profile.interact,'jump'=>_profile.jump,'dash'=>_profile.dash,'drop'=>_profile.drop,'hotbar'=>_profile.hotbar,_=>_profile.joystick};
  void _set(ControlAnchor anchor){setState(()=>_profile=_profile.update(_selected,anchor));widget.onChanged(_profile);}
  @override Widget build(BuildContext context)=>SafeArea(child:LayoutBuilder(builder:(BuildContext context,BoxConstraints c){
    Offset pos(ControlAnchor a)=>Offset(a.x*c.maxWidth,a.y*c.maxHeight);
    Widget handle(String id,String label,IconData icon,ControlAnchor a){final Offset p=pos(a);final double base=id=='hotbar'?126:64;return Positioned(left:p.dx-base*a.scale/2,top:p.dy-(id=='hotbar'?24:32)*a.scale,child:GestureDetector(
      onTap:()=>setState(()=>_selected=id),onPanUpdate:(DragUpdateDetails d){final ControlAnchor current=switch(id){'interact'=>_profile.interact,'jump'=>_profile.jump,'dash'=>_profile.dash,'drop'=>_profile.drop,'hotbar'=>_profile.hotbar,_=>_profile.joystick};_selected=id;_set(current.copyWith(x:current.x+d.delta.dx/c.maxWidth,y:current.y+d.delta.dy/c.maxHeight));},
      child:Opacity(opacity:a.opacity,child:Container(width:base*a.scale,height:(id=='hotbar'?48:64)*a.scale,decoration:BoxDecoration(color:const Color(0xB20C1217),border:Border.all(color:_selected==id?OdparDesign.accent:OdparDesign.textMuted,width:_selected==id?1.5:1),borderRadius:BorderRadius.circular(id=='hotbar'?7:40)),child:Center(child:Row(mainAxisSize:MainAxisSize.min,children:<Widget>[Icon(icon,size:16),if(id=='hotbar')...<Widget>[const SizedBox(width:5),Text(label,style:const TextStyle(fontSize:8))]]))),),
    ));}
    return Stack(children:<Widget>[
      Positioned.fill(child:ColoredBox(color:const Color(0x16000000))),
      handle('joystick','JOYSTICK',Icons.control_camera_rounded,_profile.joystick),handle('interact','INTERACT',Icons.pan_tool_alt_outlined,_profile.interact),handle('jump','SALTO',Icons.arrow_upward_rounded,_profile.jump),handle('dash','DASH',Icons.double_arrow_rounded,_profile.dash),handle('drop','SOLTAR',Icons.arrow_downward_rounded,_profile.drop),handle('hotbar','HOTBAR',Icons.view_week_outlined,_profile.hotbar),
      Positioned(left:12,right:12,top:8,child:Row(children:<Widget>[
        Container(padding:const EdgeInsets.symmetric(horizontal:10,vertical:7),decoration:BoxDecoration(color:OdparDesign.panel,border:Border.all(color:OdparDesign.panelEdge),borderRadius:BorderRadius.circular(5)),child:const Text('EDITAR CONTROLES · MUNDO PAUSADO',style:TextStyle(fontSize:8,letterSpacing:1,color:OdparDesign.textMuted))),
        const Spacer(),IconButton(style:IconButton.styleFrom(backgroundColor:OdparDesign.panel),onPressed:widget.onClose,icon:const Icon(Icons.check_rounded)),
      ])),
      Positioned(left:12,right:12,bottom:10,child:Container(padding:const EdgeInsets.all(10),decoration:BoxDecoration(color:const Color(0xF00B0F13),border:Border.all(color:OdparDesign.panelEdge),borderRadius:BorderRadius.circular(6)),child:Row(children:<Widget>[
        Text(_selected.toUpperCase(),style:const TextStyle(fontSize:8,color:OdparDesign.accent,fontWeight:FontWeight.w700)),const SizedBox(width:12),
        const Text('TAMAÑO',style:TextStyle(fontSize:7,color:OdparDesign.textMuted)),Expanded(child:Slider(value:_anchor.scale,min:.70,max:1.50,onChanged:(double v)=>_set(_anchor.copyWith(scale:v)))),
        const Text('OPACIDAD',style:TextStyle(fontSize:7,color:OdparDesign.textMuted)),Expanded(child:Slider(value:_anchor.opacity,min:.20,max:1.00,onChanged:(double v)=>_set(_anchor.copyWith(opacity:v)))),
      ]))),
    ]);
  }));
}
