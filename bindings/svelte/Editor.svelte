<!--
  What an area shows: `children` (the main view) and the regions, each prop (`header`,
  `toolHeader`, `footer`, `assetShelf`, `channels`, `navBar`, `toolbar`, `sidebar`, `execute`,
  `hud`) a snippet or `{ content: snippet, size, hidden, side, overlap, resizable, maxsize,
  hotkey, onResize }`.
  SPDX-License-Identifier: GPL-2.0-or-later
-->
<script>
  import screen from '@blender-ui/native/screen'
  import Region from './Region.svelte'

  let { children, ...regions } = $props()
  const slots = $derived(Object.entries(screen.SLOT).filter(([slot]) => regions[slot] != null)
    .map(([slot, kind]) => {
      const v = regions[slot]
      return typeof v === 'function' ? { kind, content: v } : { kind, ...v }
    }))
</script>

<bui-area>
  {#each slots as r (r.kind)}
    <Region region={r.kind} size={r.size} hidden={r.hidden} side={r.side} overlap={r.overlap}
            resizable={r.resizable} maxsize={r.maxsize} hotkey={r.hotkey} onresize={r.onResize}>
      {@render r.content?.()}
    </Region>
  {/each}
  <bui-main>{@render children?.()}</bui-main>
</bui-area>
