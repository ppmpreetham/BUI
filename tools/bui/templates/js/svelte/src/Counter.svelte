<script>
  import { Button } from '@blender-ui/svelte'

  let count = $state(0)
  let name = $state('')
</script>

<main class="flex flex-col gap-3 p-6">
  <h1 class="text-xl font-bold">{{title}}</h1>
  <div class="flex items-center gap-3">
    <span>Clicks: {count}</span>
    <Button onclick={() => count++}>Add one</Button>
  </div>
  <input placeholder="Your name" bind:value={name} />
  {#if name}<p class="text-[#9ab8e8]">Hello, {name}!</p>{/if}
</main>
