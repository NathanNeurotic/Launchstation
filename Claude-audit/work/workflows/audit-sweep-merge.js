export const meta = {
  name: 'audit-sweep-merge',
  description: 'Repo-wide sweeps (dup, dead, size, ci, features) with independent verification, then one merge pass per repo',
  phases: [
    { title: 'Sweep', detail: 'one agent per repo and sweep lens; findings JSON on disk' },
    { title: 'Verify', detail: 'one independent verifier per sweep' },
    { title: 'Merge', detail: 'one agent per repo: list each problem once, settle contradictions' },
  ],
}
const ROOT = (args && args.root) || '<WORK_ROOT>'
if (ROOT.includes('<WORK_ROOT>')) throw new Error('pass args.root: absolute path of the work root (Claude-audit/work in the PR checkout)')
// args: {repos: [...], sweeps: [...], merge: true|false}
const repos = (args && args.repos) || ['RiptOPL', 'Launchstation']
const sweeps = (args && args.sweeps) || ['dup', 'dead', 'size', 'ci', 'features']
const doMerge = !(args && args.merge === false)
const RUN_SCHEMA = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['ok', 'partial', 'failed'] },
    findings: { type: 'integer' },
    note: { type: 'string' },
  },
  required: ['status', 'findings', 'note'],
}
const VERIFY_SCHEMA = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['ok', 'partial', 'failed'] },
    confirmed: { type: 'integer' },
    corrected: { type: 'integer' },
    refuted: { type: 'integer' },
    note: { type: 'string' },
  },
  required: ['status', 'confirmed', 'corrected', 'refuted', 'note'],
}
const MERGE_SCHEMA = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['ok', 'partial', 'failed'] },
    groups: { type: 'integer' },
    absorbed: { type: 'integer' },
    drops: { type: 'integer' },
    note: { type: 'string' },
  },
  required: ['status', 'groups', 'absorbed', 'drops', 'note'],
}
const head = (repo, role) => [
  `You are ${role} in an exhaustive, read-only code audit of the ${repo} repository.`,
  `Work root: ${ROOT}. Run every command from this folder (cd into it first). Audited sources: ${ROOT}/src/${repo}/.`,
  ``,
]
function sweepPrompt(repo, s, attempt) {
  const l = head(repo, `the "${s}" sweep agent`).concat([
    `Read these files completely, in this order, before anything else:`,
    `1. ${ROOT}/AUDIT-RULES.md  (binding rules)`,
    `2. ${ROOT}/prompts/SWEEP.md  (your procedure; your lens is the section "Lens: ${s}")`,
    `3. ${ROOT}/FINDINGS-FORMAT.md  (the output format)`,
    ``,
    `Then carry out SWEEP.md for repo ${repo}, sweep name ${s} (unit id sweep-${s}), starting with its step 0.`,
  ])
  if (attempt > 1) l.push(`This is attempt ${attempt}: an earlier attempt did not finish. Pick up from tmp/${repo}/sweep-${s}.notes.md and any existing findings file; repair an invalid file instead of starting over.`)
  l.push(``, `Final answer: status "ok" only if the validator printed OK for unit sweep-${s}; "findings" is the number of findings in the file; "note" is one or two sentences (what was not covered and why; empty if nothing).`)
  return l.join('\n')
}
function verifyPrompt(repo, unit, attempt) {
  const l = head(repo, 'a verifier').concat([
    `Unit to verify: ${unit}  (findings file: ${ROOT}/findings/${repo}/${unit}.json).`,
    `Read these files completely, in this order, before anything else:`,
    `1. ${ROOT}/AUDIT-RULES.md  (binding rules, evidence standards, rating scales)`,
    `2. ${ROOT}/prompts/VERIFIER.md  (your procedure)`,
    `3. ${ROOT}/FINDINGS-FORMAT.md  (formats of the findings file and of your verification file)`,
    ``,
    `Then carry out VERIFIER.md for repo ${repo}, unit ${unit}, starting with its step 0. ${ROOT}/brief/${repo}.md describes how the repo is built and what is live or legacy. This unit is a repo-wide sweep: also refute any finding that only repeats one already reported by another unit (check with python -I scripts/digest.py . ${repo} findings --all) and say "repeat of <key>".`,
  ])
  if (attempt > 1) l.push(`This is attempt ${attempt}: an earlier attempt did not finish. If a verification file already exists but is invalid, repair it instead of starting over.`)
  l.push(``, `Final answer: status "ok" only if the validator (with --verify) printed OK; give the number of confirmed, corrected and refuted findings; "note" is one or two sentences on anything unusual (empty if nothing).`)
  return l.join('\n')
}
function mergePrompt(repo, attempt) {
  const l = head(repo, 'the merge agent').concat([
    `Read these files completely, in this order, before anything else:`,
    `1. ${ROOT}/AUDIT-RULES.md  (binding rules and rating scale)`,
    `2. ${ROOT}/prompts/MERGE.md  (your procedure)`,
    `3. ${ROOT}/FINDINGS-FORMAT.md  (field formats and text limits)`,
    ``,
    `Then carry out MERGE.md for repo ${repo}.`,
  ])
  if (attempt > 1) l.push(`This is attempt ${attempt}: an earlier attempt did not finish. If findings/${repo}/_merge.json exists, check it with check_merge.py and repair or extend it instead of starting over.`)
  l.push(``, `Final answer: status "ok" only if check_merge.py printed OK; give the number of groups, absorbed findings and drops; "note" is one or two sentences (empty if nothing).`)
  return l.join('\n')
}
async function twice(make, opts, label) {
  let r = await agent(make(1), { ...opts, label })
  if (!r || r.status !== 'ok') {
    log(`${label}: ${r ? r.status : 'did not return'}; second attempt`)
    r = await agent(make(2), { ...opts, label: label + ' (2)' })
  }
  return r
}
const out = await parallel(repos.map(repo => async () => {
  const sw = await parallel(sweeps.map(s => async () => {
    const unit = `sweep-${s}`
    const r = await twice(a => sweepPrompt(repo, s, a), { phase: 'Sweep', schema: RUN_SCHEMA, effort: 'high' }, `${repo} ${unit}`)
    const res = { unit, sweep: r ? r.status : 'died', findings: r ? r.findings : 0, note: r ? r.note : '' }
    if (res.sweep !== 'ok' || !res.findings) return res
    const v = await twice(a => verifyPrompt(repo, unit, a), { phase: 'Verify', schema: VERIFY_SCHEMA, effort: 'high' }, `${repo} verify ${unit}`)
    return { ...res, verify: v ? v.status : 'died', confirmed: v ? v.confirmed : 0, corrected: v ? v.corrected : 0, refuted: v ? v.refuted : 0, verifyNote: v ? v.note : '' }
  }))
  let merge = null
  if (doMerge) {
    const m = await twice(a => mergePrompt(repo, a), { phase: 'Merge', schema: MERGE_SCHEMA, effort: 'xhigh' }, `${repo} merge`)
    merge = m || { status: 'died' }
  }
  log(`${repo}: sweeps ${sw.filter(Boolean).map(x => x.unit + '=' + x.sweep).join(', ')}; merge ${merge ? merge.status : 'skipped'}`)
  return { repo, sweeps: sw, merge }
}))
return out
