<!--
  Areas the user splits, joins, docks and resizes as in Blender. `editors`: name ->
  `{ icon, render }`, `render` a snippet taking the area (`{ index, editor }`) and showing an
  <Editor>; `layout`: the first arrangement (`AreaSplit.v/h` or a name); `saved`: a layout
  `onchange` gave; `onchange({ layout })`, `onwindow({ editor })`; `ops="resize"`.
  SPDX-License-Identifier: GPL-2.0-or-later
-->
<script>
  import screen from '@blender-ui/native/screen'

  let { editors, layout, saved, ops, onchange, onwindow, ...rest } = $props()
  let areas = $state([])
  const handle = (e) => {
    if (e.target.localName !== 'bui-screen') return
    const c = screen.screenChange(e.target.value)
    if (c === null) return
    if (c.window !== undefined) {
      onwindow?.({ editor: c.window })
      return
    }
    areas = c.areas
    onchange?.({ layout: c.layout })
  }
</script>

<bui-screen {...rest} layout={screen.layoutText(layout)} {saved} {ops} onchange={handle}>
  {#each areas as editor, index (`${index}:${editor}`)}
    {@render screen.editorFor(editors, editor).render({ index, editor })}
  {/each}
</bui-screen>
