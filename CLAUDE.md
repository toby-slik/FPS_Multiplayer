# FPS_Multiplayer

Unreal Engine 5 project. C++ source in `Source/FPS`, content in `Content/FPS`.

## Design source of truth

The game design document below is authoritative for genre, pillars, progression and art
direction. Do not invent design intent that contradicts it — if something is missing, it is
genuinely undecided (marked TBD) and worth asking about.

@gdd/gdd.md

## Specialist agents

Some work has dedicated subagents in `.claude/agents/` with the design context already baked
in — prefer them over doing the work inline:

- `player-movement` — sprint, slide, jump/wall-jump, movement tuning on `AShooterCharacter`
- `level-designer` — level blockout and geometry via the Unreal MCP editor connection
