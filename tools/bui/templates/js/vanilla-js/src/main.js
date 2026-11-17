import { App } from '@blender-ui/native'

const app = new App()
const win = app.window({ title: '{{title}}', width: 360, height: 220 })
win.body.setAttribute('styled', '')
win.body.className = 'm-0 flex flex-col gap-3 bg-[#2b2b2b] p-6 text-[#e6e6e6]'

const title = win.createElement('h1')
title.className = 'text-xl font-bold'
title.textContent = '{{title}}'

const row = win.createElement('div')
row.className = 'flex items-center gap-3'
const label = win.createElement('span')
label.textContent = 'Clicks: 0'
const button = win.createElement('button')
button.textContent = 'Add one'

let clicks = 0
button.addEventListener('click', () => {
  clicks += 1
  label.textContent = `Clicks: ${clicks}`
})

row.appendChild(label)
row.appendChild(button)
win.body.appendChild(title)
win.body.appendChild(row)
