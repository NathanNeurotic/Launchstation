export const meta = {
  name: 'audit-read-verify',
  description: 'Read one part of a repo line by line (one reader per unit), write findings to disk, then independently re-check every finding',
  phases: [
    { title: 'Read', detail: 'one reader per unit: assigned files plus a lens; findings JSON on disk' },
    { title: 'Verify', detail: 'one independent verifier per unit tries to refute each finding' },
  ],
}

const ROOT = (args && args.root) || '<WORK_ROOT>'
if (ROOT.includes('<WORK_ROOT>')) throw new Error('pass args.root: absolute path of the work root (Claude-audit/work in the PR checkout)')
const PARTS = __PARTS__

// args: {part: '<name>'} for a predefined part, or {part: '<label>', units: ['Repo:unit:tier:lines:nfiles', ...]}
const part = args.part
const key = part
// optional args.repo: keep only that repo's units (a cloud checkout holds one repo)
const list = (args.units || PARTS[part] || []).filter(s => !args.repo || s.startsWith(args.repo + ':'))
if (!list.length) throw new Error('no units for part ' + part + (args.repo ? ' and repo ' + args.repo : ''))

const units = list.map(s => {
  const [repo, id, tier, lines, nfiles] = s.split(':')
  const defects = id.endsWith('.b')
  return {
    repo,
    id,
    tier,
    lens: defects ? 'defects' : (tier === 'core' || tier === 'module') ? 'structure' : tier,
    lines: Number(lines),
    nfiles: Number(nfiles),
  }
})

const READ_SCHEMA = {
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

function readPrompt(u, attempt) {
  const repo = u.repo
  const lines = [
    `You are a reader in an exhaustive, read-only code audit of the ${repo} repository.`,
    `Work root: ${ROOT}. Run every command from this folder (cd into it first).`,
    `Your unit id: ${u.id}  (tier ${u.tier}, lens ${u.lens}, about ${u.lines} lines in ${u.nfiles} file(s)).`,
    ``,
    `Read these four files completely, in this order, before anything else:`,
    `1. ${ROOT}/AUDIT-RULES.md  (binding rules)`,
    `2. ${ROOT}/prompts/READER.md  (the procedure every reader follows)`,
    `3. ${ROOT}/prompts/lens-${u.lens}.md  (what you look for on this unit)`,
    `4. ${ROOT}/FINDINGS-FORMAT.md  (the output format)`,
    ``,
    `Then carry out READER.md for repo ${repo}, unit ${u.id}, starting with its step 0.`,
  ]
  if (attempt > 1) {
    lines.push(`This is attempt ${attempt}: an earlier attempt on this unit did not finish. Steps 0 and 3 of READER.md say how to pick up what it left behind (a finished findings file, or a notes file).`)
  }
  lines.push(
    ``,
    `Final answer: status "ok" only if the validator printed OK for unit ${u.id}; "findings" is the number of findings in the file; "note" is one or two sentences (what was not fully read and why, or what went wrong; empty if nothing).`
  )
  return lines.join('\n')
}

function verifyPrompt(u, attempt) {
  const repo = u.repo
  const lines = [
    `You are a verifier in an exhaustive, read-only code audit of the ${repo} repository.`,
    `Work root: ${ROOT}. Run every command from this folder (cd into it first).`,
    `Unit to verify: ${u.id}  (findings file: ${ROOT}/findings/${repo}/${u.id}.json; audited sources: ${ROOT}/src/${repo}/).`,
    ``,
    `Read these three files completely, in this order, before anything else:`,
    `1. ${ROOT}/AUDIT-RULES.md  (binding rules, evidence standards, rating scales)`,
    `2. ${ROOT}/prompts/VERIFIER.md  (your procedure)`,
    `3. ${ROOT}/FINDINGS-FORMAT.md  (formats of the findings file and of your verification file)`,
    ``,
    `Then carry out VERIFIER.md for repo ${repo}, unit ${u.id}, starting with its step 0. ${ROOT}/brief/${repo}.md describes how the repo is built and what is live or legacy; use it when a verdict depends on that.`,
  ]
  if (attempt > 1) {
    lines.push(`This is attempt ${attempt}: an earlier attempt did not finish. If a verification file already exists but is invalid, repair it instead of starting over.`)
  }
  lines.push(
    ``,
    `Final answer: status "ok" only if the validator (with --verify) printed OK; give the number of confirmed, corrected and refuted findings; "note" is one or two sentences on anything unusual (empty if nothing).`
  )
  return lines.join('\n')
}

log(`${key}: ${units.length} units`)

const results = await pipeline(
  units,
  async (u) => {
    const opts = { phase: 'Read', schema: READ_SCHEMA, effort: u.lens === 'defects' ? 'xhigh' : 'high' }
    let r = await agent(readPrompt(u, 1), { ...opts, label: `read ${u.id}` })
    if (!r || r.status !== 'ok') {
      log(`${u.id}: reader ${r ? r.status : 'did not return'}; second attempt`)
      r = await agent(readPrompt(u, 2), { ...opts, label: `read ${u.id} (2)` })
    }
    return { repo: u.repo, unit: u.id, read: r ? r.status : 'died', findings: r ? r.findings : 0, readNote: r ? r.note : '' }
  },
  async (prev, u) => {
    if (prev.read !== 'ok') return { ...prev, verify: 'skipped' }
    if (!prev.findings) return { ...prev, verify: 'nothing-to-verify' }
    const opts = { phase: 'Verify', schema: VERIFY_SCHEMA, effort: u.lens === 'defects' ? 'xhigh' : 'high' }
    let v = await agent(verifyPrompt(u, 1), { ...opts, label: `verify ${u.id}` })
    if (!v || v.status !== 'ok') {
      log(`${u.id}: verifier ${v ? v.status : 'did not return'}; second attempt`)
      v = await agent(verifyPrompt(u, 2), { ...opts, label: `verify ${u.id} (2)` })
    }
    return {
      ...prev,
      verify: v ? v.status : 'died',
      confirmed: v ? v.confirmed : 0,
      corrected: v ? v.corrected : 0,
      refuted: v ? v.refuted : 0,
      verifyNote: v ? v.note : '',
    }
  }
)

const done = results.filter(Boolean)
const notOk = done.filter(r => r.read !== 'ok' || (r.verify !== 'ok' && r.verify !== 'nothing-to-verify'))
log(`${key}: ${done.length}/${units.length} units returned, ${notOk.length} need attention`)
return {
  part,
  units: units.length,
  dropped: results.length - done.length,
  findings: done.reduce((a, r) => a + (r.findings || 0), 0),
  refuted: done.reduce((a, r) => a + (r.refuted || 0), 0),
  needAttention: notOk,
  notes: done.filter(r => r.readNote || r.verifyNote).map(r => ({ unit: r.unit, read: r.readNote, verify: r.verifyNote })),
}
