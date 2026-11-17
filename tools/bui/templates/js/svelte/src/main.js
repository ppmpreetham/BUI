import { render } from '@blender-ui/svelte'
import Counter from './Counter.svelte'

const { window } = render(Counter, { title: '{{title}}', width: 420, height: 260 })
window.body.setAttribute('styled', '')
window.body.className = 'm-0 min-h-screen bg-[#2b2b2b] text-[#e6e6e6]'
