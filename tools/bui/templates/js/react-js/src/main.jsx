/** @jsxImportSource @blender-ui/react */
import { render, useState } from '@blender-ui/react'

function Counter() {
  const [count, setCount] = useState(0)
  const [name, setName] = useState('')
  return (
    <main className="flex flex-col gap-3 p-6">
      <h1 className="text-xl font-bold">{{title}}</h1>
      <div className="flex items-center gap-3">
        <span>Clicks: {count}</span>
        <button type="button" onClick={() => setCount((c) => c + 1)}>Add one</button>
      </div>
      <input placeholder="Your name" value={name} onChange={(e) => setName(e.target.value)} />
      {name.value && <p className="text-[#9ab8e8]">Hello, {name}!</p>}
    </main>
  )
}

const { window } = render(<Counter />, { title: '{{title}}', width: 420, height: 260 })
window.body.setAttribute('styled', '')
window.body.className = 'm-0 min-h-screen bg-[#2b2b2b] text-[#e6e6e6]'
