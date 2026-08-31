# Game Design Document

**Revision:** 0.4 - Draft  
**Date:** 2026-08-31  
**Working title:** Gun Thief / Deadman's Loadout / Spoils - TBD  
**Genre / positioning:** Movement shooter for PC and console, played solo against AI or 1v1 against another player. Round-to-round pacing and movement tech in the spirit of competitive tactical shooters, with a Rocket League-style ranked ladder and an item-stakes twist.  
**Setting:** High-fidelity near-future sci-fi with sleek, well-kept hardware, staged in modernist concrete architecture integrated with cascading water and greenery rather than grim or derelict environments.  
**Audience:** Internal design and engineering team. Open questions are marked as TBD rather than answered speculatively.

---

## 1. High Concept

A persistent 1v1 movement shooter. Players keep their weapons and attachments by winning matches. Better weapons and attachments can be earned, but valuable items can also be stolen after a loss.

Players fight in ranked levels called **tiers**, earning enough wins to move into higher tiers with better equipment and stronger players.

---

## 2. Design Pillars

### 2.1 Consistent Movement

All players have identical movement abilities from the start:

- Running
- Jumping
- Sliding
- Wall jumping

Players improve by mastering the shared movement system, not by unlocking stronger movement. Equipment and progression never provide movement-stat bonuses.

Higher-tier special abilities, such as camo, dash, or rewind, are deferred to a later design pass and are out of scope for this document.

### 2.2 Skill or Persistence

A player advances to the next tier by reaching either:

- **5 wins in a row**, or
- **20 total wins** in the current tier

Strong players move through lower tiers quickly by maintaining a streak. Persistent players still progress at their own pace. Losses never remove total wins or cause demotion.

### 2.3 Valuable Loadouts

Weapons and attachments persist between matches. Items above the current tier's protected baseline can be stolen after a loss.

Stealing is always the winner's choice. The winner may keep their own gear and decline to take anything from the loser.

---

## 3. Art and Level Direction

### Theme

Beautiful, high-fidelity near-future sci-fi. The game should feel like a sleek traversal fantasy with well-kept, premium hardware rather than a grim or derelict setting. Facilities feel engineered, cared for, and inhabited.

### Level Design Language

Modernist brutalist architecture built with the landscape rather than against it. Environments use board-formed concrete, cantilevered decks, and monumental structures set into natural terrain.

Cascading water, reflecting pools, and greenery run through the architecture. Waterfalls cut across sightlines, terraces overhang ravines, and vegetation softens hard concrete edges.

| Architectural or natural feature | Gameplay translation |
|---|---|
| Cantilevered concrete decks over water | Elevated traversal lanes and wall-jump surfaces above open sightlines |
| Waterfalls and cascades cutting through structures | Visual and audio cover, plus dynamic sliding routes through spray |
| Terraced greenery and planted courtyards | Soft-cover foliage that breaks up long concrete sightlines |
| Stepped massing and terraced levels | Multi-tier verticality for jump and wall-jump chains |
| Reflecting pools, glass, and water surfaces | Readable landmarks for map callouts and orientation |

### Palette

Warm travertine and board-formed concrete, deep teal water, verdant greenery, and golden natural light. Cool blue-grey shadows are reserved mainly for interiors.

Hardware and UI accents use clean teal and gold drawn from the environment rather than hazard-industrial colors.

### UI and HUD Direction

Sleek, minimal, and high-tech, with clean linework and confident typography. Avoid stencil or blueprint-style signage.

Accent colors should come from the world, especially water teal and sunlit gold, so the HUD and environment feel like one visual system.

> **TBD:** Decide which architectural and landscape identity belongs to each tier.

---

## 4. Core Gameplay Loop

This loop describes the 1v1 multiplayer mode. The single-player campaign loop is
described in section 5.

1. Match against another player in the same tier.
2. Choose a loadout.
3. Play a 1v1 match.
4. The winner gains one tier win.
5. The winner may steal one item from the loser or decline.
6. Offer a rematch.
7. Check for tier advancement.
8. Rematch or return to matchmaking.

---

## 5. Single-Player Campaign

Playing alone against AI is a designed single-player experience, not a practice mode against
a bot in a multiplayer arena. It is the game's introduction to the movement system, the
weapons and the combat pacing.

### Structure

The campaign is a sequence of levels. Each level is one combat arena holding one AI opponent.

1. The player enters the arena and fights a single AI bot.
2. Killing the bot opens the door out of the arena.
3. A short traversal section runs between arenas, built for the movement tech - sliding,
   jumping, wall jumping - and containing no combat.
4. The traversal ends at the next arena, which holds a harder bot.

The first arena is deliberately easy: an entry-level bot in a simple, readable space, so the
player can learn to shoot and move without pressure. Each subsequent arena raises bot
difficulty.

### Escalation

Difficulty rises along two axes at once:

| Axis | Progression |
|---|---|
| AI opponent | Slower reactions and poorer aim early, sharper aim and tactical use of movement tech later |
| Traversal | Short and forgiving early, longer chained wall jumps and slides later |

### Weapon Progression

The player starts the campaign with a pistol only. Better weapons are unlocked as the
campaign progresses, so each new arena is fought with a slightly wider kit than the last.

This mirrors the tier baseline in section 6: campaign weapon unlocks follow the same rarity
ladder rather than a separate one.

An unlocked weapon is granted on entering the level that unlocks it. The player's kit at any
level is the union of every unlock up to and including that level, so the list of levels is the
whole progression - there is no separate unlock ledger to keep in sync.

### Relationship to the Ranked Ladder

The campaign is a **separate game mode** from the 1v1 ladder. It has its own progression and
its own save data. Campaign weapon unlocks do not enter the persistent multiplayer inventory,
campaign progress does not count toward tier advancement, and nothing in the campaign can be
stolen. The two modes share only the character, the movement system, the weapons and the bot.

### Failure

Dying restarts the current level. The arena is fought from the top rather than resumed against
a bot that is still half dead. Campaign progress up to that level is not lost.

> **TBD:** Total number of campaign levels.

---

## 6. Level and Tier Structure

Each tier defines:

- A baseline weapon rarity
- A baseline attachment rarity
- A pool of available weapons
- New equipment to unlock
- A new map

Players only match against others in the same tier.

Tier, wins, win streak, and unlocked equipment persist between sessions. The ladder is fully persistent.

---

## 7. Equipment Baseline and Stealing Rules

Each tier has a protected baseline rarity.

Items at or below that baseline cannot be stolen. Only items above the baseline are eligible. This allows players to lose valuable gear without ever falling below the minimum equipment standard for their tier.

Attachments can also randomly roll to a higher rarity at the start of a round. This is a temporary bonus for that round only.

- The holder keeps the upgraded attachment if they win.
- The attachment goes to the victor if the holder loses.

---

## 8. Winner's Choice

If the loser has any items above the protected baseline, the winner sees the loser's two highest-value stealable items and has 10 seconds to choose one of the following:

- Steal the first offered item
- Steal the second offered item
- Take nothing

No selection within the time limit is treated as declining. Nothing is stolen.

Both players see:

- The two offered items
- Whether the winner stole an item or declined
- Which item was stolen
- The result of duplicate-item handling

The loser may request one rematch to try to win the item back. The winner may accept and continue their win streak.

If the loser has no items above the baseline, no choice is presented.

---

## 9. Duplicate Items

At the start of a match, the game checks both players' inventories and upgrades matching items so neither player is holding a duplicate of the other's item when stealing occurs.

This ensures that a stolen item is always a meaningful addition to the winner's inventory rather than a useless duplicate.

> **TBD:** Define which duplicate is upgraded and by how much.

---

## 10. New Weapon Pickups

Some tiers introduce new weapons as arena pickups.

| Condition | Result |
|---|---|
| Finish the match holding the weapon, whether the player wins or loses | Unlock the weapon at base rarity |
| Win while holding the weapon | Unlock the weapon one rarity higher |

The unlocked weapon becomes available in the loadout selector.

A carried-forward rarity upgrade applies automatically at the next tier. A player who has not yet advanced but is still holding the weapon can still receive the upgrade at their current tier.

---

## 11. Loadouts

The first tier gives all players identical starting equipment and does not use a loadout selector.

From a later tier onward, players choose from the weapons and attachments they have unlocked.

Equipment should support different playstyles rather than provide simple power increases.

| Trade-off A | Trade-off B |
|---|---|
| Larger magazine | Faster reload |
| Better hip-fire accuracy | Better ranged accuracy |
| Faster weapon swapping | Lower recoil |
| Better airborne handling | Better grounded handling |

---

## 12. Matchmaking

Tier determines the equipment pool a player has reached.

Matchmaking uses an open queue within each tier. Any player in a tier can be matched against any other player in that tier.

The 5-win-streak route allows highly skilled players to move through early tiers quickly. The 20-total-wins route allows persistent players to progress without being penalized for losses.

---

## 13. Post-Match Flow

1. Add the winner's tier win.
2. Update both players' win streaks.
3. If the loser has stealable items, show the two available items.
4. Let the winner steal one item or take nothing.
5. Show the result to both players.
6. Offer a rematch or return to matchmaking.
7. Check for tier advancement.

---

## 14. Open Design Questions

| Topic | Question |
|---|---|
| Title | Final working title: Gun Thief, Deadman's Loadout, Spoils, or another option - TBD |
| Special abilities | Camo, dash, or rewind; which tier introduces them, and how do they fit the consistent-movement pillar? - TBD |
| Duplicate resolution | What is the exact upgrade rule when both players hold the same item? - TBD |
| Rematch to reclaim | What happens if the winner declines the rematch or the loser does not request it in time? - TBD |
| Tier count and pacing | What is the total number of tiers, and what is the rarity and tier progression curve? - TBD |
| Anti-farming and smurfing | What safeguards prevent deliberate loss-farming, item transfers, or heavily geared alternate accounts? - TBD |
| Platform and input | How will crossplay and controller versus mouse-and-keyboard parity work for movement mechanics? - TBD |
| Map roster | Which architectural and landscape identity belongs to each tier? - TBD |
| Campaign length | How many campaign levels, and how does bot difficulty scale across them? - TBD |
