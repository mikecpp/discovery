<script setup>
import { useRoute } from 'vue-router'

const route = useRoute()
</script>

<template>
  <div class="card">
    <p>The requested page <code>{{ route.path }}</code> was not found.</p>
    <RouterLink class="button" to="/devices">Go to Devices</RouterLink>
  </div>
</template>
