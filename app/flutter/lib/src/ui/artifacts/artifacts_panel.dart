import 'dart:async';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../../engine/game_runtime.dart';
import '../../engine/game_snapshot.dart';
import '../../native/odg_bindings.dart';
import '../design_system.dart';
import '../item_glyph.dart';

final class ArtifactsPanel extends StatefulWidget {
  const ArtifactsPanel({required this.runtime, super.key});
  final GameRuntime runtime;
  @override State<ArtifactsPanel> createState() => _ArtifactsPanelState();
}

final class _ArtifactsPanelState extends State<ArtifactsPanel> {
  int? _remoteId;
  ui.Image? _remoteImage;
  Timer? _remoteTimer;
  bool _renderBusy = false;

  @override void dispose() { _remoteTimer?.cancel(); _remoteImage?.dispose(); super.dispose(); }

  void _openRemote(GameArtifact artifact) {
    _remoteTimer?.cancel(); _remoteImage?.dispose(); _remoteImage = null;
    setState(() => _remoteId = artifact.artifactId);
    _renderRemote();
    _remoteTimer = Timer.periodic(const Duration(milliseconds: 80), (_) => _renderRemote());
  }

  void _closeRemote() {
    _remoteTimer?.cancel(); _remoteTimer = null; _remoteImage?.dispose(); _remoteImage = null;
    if (mounted) setState(() => _remoteId = null);
  }

  Future<void> _renderRemote() async {
    final int? id = _remoteId;
    if (id == null || _renderBusy || !mounted) return;
    _renderBusy = true;
    try {
      final ui.Image? image = await widget.runtime.renderArtifactView(id);
      if (!mounted || id != _remoteId) { image?.dispose(); return; }
      final ui.Image? old = _remoteImage;
      setState(() => _remoteImage = image);
      old?.dispose();
    } finally { _renderBusy = false; }
  }

  @override Widget build(BuildContext context) {
    final List<GameArtifact> owned = widget.runtime.snapshot.artifacts.where((GameArtifact a) => a.ownerActorId == 0).toList(growable: false);
    if (_remoteId != null) {
      return Column(children: <Widget>[
        Padding(padding: const EdgeInsets.fromLTRB(14, 10, 8, 8), child: Row(children: <Widget>[
          const Expanded(child: Text('CÁMARA REMOTA · TORRETA', style: TextStyle(fontSize: 10, letterSpacing: 1.1, color: OdparDesign.textMuted))),
          IconButton(onPressed: _closeRemote, icon: const Icon(Icons.close_rounded)),
        ])),
        Expanded(child: Center(child: AspectRatio(aspectRatio: 16/9, child: DecoratedBox(
          decoration: BoxDecoration(color: Colors.black, border: Border.all(color: OdparDesign.panelEdge), borderRadius: BorderRadius.circular(7)),
          child: _remoteImage == null ? const Center(child: CircularProgressIndicator(strokeWidth: 1.5)) : RawImage(image: _remoteImage, fit: BoxFit.cover, filterQuality: FilterQuality.medium),
        )))),
        const Padding(padding: EdgeInsets.all(12), child: Text('320×180 · 12.5 FPS · RENDER C', style: TextStyle(fontSize: 8, letterSpacing: 1.1, color: OdparDesign.textMuted))),
      ]);
    }
    if (owned.isEmpty) return const Center(child: Text('SIN ARTEFACTOS DESPLEGADOS', style: TextStyle(color: OdparDesign.textMuted, fontSize: 10)));
    return ListView.separated(
      padding: const EdgeInsets.all(14), itemCount: owned.length,
      separatorBuilder: (_, __) => const SizedBox(height: 7),
      itemBuilder: (BuildContext context, int index) {
        final GameArtifact artifact=owned[index];
        final bool remote=(artifact.capabilityBits & odgArtifactCapRemoteView)!=0;
        final bool turret=artifact.itemType==odgItemTurret;
        final int turretMaxAmmo=artifact.state & odgTurretArtifactStateMaxAmmoMask;
        final int turretMode=(artifact.state >> odgTurretArtifactStateModeShift) & 0xffff;
        final String suffix=turret
            ? '${artifact.storageUsed}/$turretMaxAmmo munición · ${turretMode==odgTurretModeHarvest?'TALA':'DEFENSA'}'
            : '${artifact.xMilli~/1000}, ${artifact.zMilli~/1000} m';
        return Container(
          padding: const EdgeInsets.all(11),
          decoration: BoxDecoration(color: const Color(0xFF10161B), border: Border.all(color: OdparDesign.panelEdge), borderRadius: BorderRadius.circular(6)),
          child: Row(children: <Widget>[
            OdparItemGlyph(typeId:artifact.itemType,materialTier:artifact.materialTier,size:24,emphasized:true),
            const SizedBox(width: 10),
            Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: <Widget>[
              Text('${artifact.name}${artifact.materialTier==0?'':' · ${_tier(artifact.materialTier)}'}', style: const TextStyle(fontSize: 11, fontWeight: FontWeight.w700)),
              const SizedBox(height: 3), Text(suffix, style: const TextStyle(fontSize: 8, color: OdparDesign.textMuted)),
            ])),
            if(turret && (artifact.capabilityBits & odgArtifactCapHarvest)!=0)
              TextButton.icon(
                onPressed:()=>widget.runtime.setTurretMode(artifact.artifactId, turretMode==odgTurretModeHarvest?odgTurretModeDefense:odgTurretModeHarvest),
                icon:Icon(turretMode==odgTurretModeHarvest?Icons.park_outlined:Icons.shield_outlined,size:15),
                label:Text(turretMode==odgTurretModeHarvest?'TALA':'DEFENSA',style:const TextStyle(fontSize:8,letterSpacing:.7)),
              ),
            if(remote) IconButton(tooltip:'Abrir cámara',onPressed:()=>_openRemote(artifact),icon:const Icon(Icons.videocam_outlined,size:19)),
          ]),
        );
      },
    );
  }

  String _tier(int tier)=>switch(tier){odgMaterialWood=>'Madera',odgMaterialStone=>'Piedra',odgMaterialIron=>'Hierro',_=>'Tech'};
}
