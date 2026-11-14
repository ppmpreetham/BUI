<!--
  A region of an <Editor> (`region`: its kind, which the Editor's prop names): `size`, `hidden`,
  `side`, `overlap`, `resizable`, `maxsize`, `hotkey`, `onresize({ size, hidden })`.
  SPDX-License-Identifier: GPL-2.0-or-later
-->
<script>
  import screen from '@blender-ui/native/screen'

  let { region, children, onresize, ...props } = $props()
  const onchange = (e) => {
    if (e.target.localName === 'bui-region') onresize?.({ size: e.target.valueAsNumber, hidden: e.target.checked })
  }
</script>

<bui-region {region} {...screen.regionAttrs(props)} {onchange}>{@render children?.()}</bui-region>
