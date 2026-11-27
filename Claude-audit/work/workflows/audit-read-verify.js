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
const PARTS = {
 "wave-A": [
  "Launchstation:launch-core-01:core:3595:14",
  "Launchstation:launch-core-02:core:2017:17",
  "RiptOPL:ript-core-03.b:core:3078:5",
  "RiptOPL:ript-core-05:core:2636:8",
  "RiptOPL:ript-core-06:core:3537:38",
  "Launchstation:launch-core-05.b:core:3377:5",
  "Launchstation:launch-core-06:core:3665:1",
  "Launchstation:launch-core-07.b:core:3455:3",
  "Launchstation:launch-core-08:core:3318:4",
  "Launchstation:launch-core-09:core:3371:4",
  "RiptOPL:ript-core-13.b:core:3066:5",
  "RiptOPL:ript-core-15:core:3068:8",
  "Launchstation:launch-core-11.b:core:2542:3",
  "Launchstation:launch-core-12:core:4103:1",
  "Launchstation:launch-core-13:core:2968:6",
  "RiptOPL:ript-core-20:core:3264:25",
  "Launchstation:launch-core-14.b:core:3291:2",
  "RiptOPL:ript-core-22:core:2791:4",
  "RiptOPL:ript-core-23.b:core:2221:6",
  "RiptOPL:ript-core-24.b:core:3562:5",
  "RiptOPL:ript-core-26:core:3483:6",
  "RiptOPL:ript-core-27:core:2759:8",
  "RiptOPL:ript-core-28:core:3474:2",
  "Launchstation:launch-core-20:core:3517:8",
  "RiptOPL:ript-core-30.b:core:3446:13",
  "RiptOPL:ript-core-32:core:3259:2",
  "RiptOPL:ript-core-33:core:1646:2",
  "Launchstation:launch-core-23.b:core:3437:11",
  "Launchstation:launch-core-24:core:3228:8",
  "Launchstation:launch-core-25:core:3242:11",
  "RiptOPL:ript-core-37.b:core:3470:6",
  "RiptOPL:ript-core-39:core:3531:2",
  "RiptOPL:ript-core-40:core:1737:7",
  "RiptOPL:ript-core-41.b:core:3484:2",
  "RiptOPL:ript-core-42.b:core:1017:2",
  "Launchstation:launch-core-30.b:core:2127:6",
  "RiptOPL:ript-module-03:module:3070:23",
  "Launchstation:launch-module-01:module:4524:18",
  "RiptOPL:ript-module-05:module:4803:21",
  "RiptOPL:ript-module-06:module:3490:19",
  "RiptOPL:ript-module-07:module:4610:26",
  "Launchstation:launch-module-04:module:4612:24",
  "RiptOPL:ript-module-09.b:module:4690:18",
  "RiptOPL:ript-module-11:module:3340:47",
  "Launchstation:launch-module-06:module:4763:31",
  "Launchstation:launch-module-07:module:3199:13",
  "RiptOPL:ript-module-13.b:module:3385:21",
  "Launchstation:launch-module-08.b:module:4070:20",
  "Launchstation:launch-module-09.b:module:4025:22",
  "Launchstation:launch-module-10:module:3855:5",
  "RiptOPL:ript-module-18.b:module:4932:25",
  "RiptOPL:ript-module-20.b:module:3550:51",
  "RiptOPL:ript-module-21.b:module:3785:21",
  "RiptOPL:ript-module-23:module:4753:33",
  "RiptOPL:ript-module-24:module:3465:17",
  "Launchstation:launch-module-15.b:module:1593:5",
  "Launchstation:launch-buildci-01:buildci:2173:11",
  "Launchstation:launch-support-01:support:7381:52",
  "Launchstation:launch-support-03:support:7973:51",
  "RiptOPL:ript-support-07:support:7996:30",
  "RiptOPL:ript-support-09:support:6965:109",
  "RiptOPL:ript-docs-01:docs:11501:59",
  "RiptOPL:ript-skim-01:skim:24210:36",
  "Launchstation:launch-skim-03:skim:7157:10"
 ],
 "wave-B": [
  "Launchstation:launch-core-01.b:core:3595:14",
  "Launchstation:launch-core-02.b:core:2017:17",
  "RiptOPL:ript-core-04:core:2922:5",
  "Launchstation:launch-core-04:core:3198:5",
  "RiptOPL:ript-core-06.b:core:3537:38",
  "RiptOPL:ript-core-08:core:2134:1",
  "Launchstation:launch-core-06.b:core:3665:1",
  "RiptOPL:ript-core-10.b:core:2610:8",
  "Launchstation:launch-core-08.b:core:3318:4",
  "RiptOPL:ript-core-13:core:3066:5",
  "RiptOPL:ript-core-14.b:core:2508:11",
  "Launchstation:launch-core-11:core:2542:3",
  "RiptOPL:ript-core-16.b:core:3322:6",
  "RiptOPL:ript-core-18:core:2726:2",
  "Launchstation:launch-core-13.b:core:2968:6",
  "RiptOPL:ript-core-20.b:core:3264:25",
  "Launchstation:launch-core-15:core:3414:6",
  "RiptOPL:ript-core-23:core:2221:6",
  "Launchstation:launch-core-16.b:core:2884:8",
  "Launchstation:launch-core-17.b:core:2630:2",
  "RiptOPL:ript-core-26.b:core:3483:6",
  "Launchstation:launch-core-19:core:2546:2",
  "RiptOPL:ript-core-29:core:3453:6",
  "Launchstation:launch-core-20.b:core:3517:8",
  "RiptOPL:ript-core-31:core:1208:4",
  "RiptOPL:ript-core-32.b:core:3259:2",
  "Launchstation:launch-core-23:core:3437:11",
  "RiptOPL:ript-core-35:core:3177:7",
  "Launchstation:launch-core-24.b:core:3228:8",
  "RiptOPL:ript-core-37:core:3470:6",
  "RiptOPL:ript-core-38.b:core:1565:11",
  "RiptOPL:ript-core-39.b:core:3531:2",
  "RiptOPL:ript-core-41:core:3484:2",
  "RiptOPL:ript-core-42:core:1017:2",
  "Launchstation:launch-core-29:core:3098:9",
  "RiptOPL:ript-module-01:module:3181:19",
  "RiptOPL:ript-module-03.b:module:3070:23",
  "RiptOPL:ript-module-04.b:module:3065:19",
  "Launchstation:launch-module-02.b:module:3607:34",
  "Launchstation:launch-module-03:module:3092:6",
  "RiptOPL:ript-module-08:module:4752:19",
  "Launchstation:launch-module-04.b:module:4612:24",
  "RiptOPL:ript-module-10:module:3833:7",
  "RiptOPL:ript-module-11.b:module:3340:47",
  "RiptOPL:ript-module-12.b:module:5000:35",
  "RiptOPL:ript-module-14:module:4645:31",
  "RiptOPL:ript-module-15:module:4001:17",
  "RiptOPL:ript-module-16:module:3732:25",
  "RiptOPL:ript-module-17.b:module:3928:19",
  "Launchstation:launch-module-11:module:4754:26",
  "RiptOPL:ript-module-19.b:module:3128:19",
  "Launchstation:launch-module-12.b:module:2781:15",
  "Launchstation:launch-module-13.b:module:4398:1",
  "Launchstation:launch-module-14:module:3714:5",
  "RiptOPL:ript-module-24.b:module:3465:17",
  "RiptOPL:ript-buildci-01:buildci:3168:11",
  "RiptOPL:ript-support-03:support:7861:35",
  "Launchstation:launch-support-04:support:7963:56",
  "RiptOPL:ript-support-08:support:7657:37",
  "Launchstation:launch-support-07:support:2351:27",
  "Launchstation:launch-docs-01:docs:8408:83",
  "RiptOPL:ript-docs-03:docs:5728:61",
  "RiptOPL:ript-skim-02:skim:16720:57"
 ],
 "wave-C": [
  "RiptOPL:ript-core-01:core:2266:36",
  "RiptOPL:ript-core-02:core:3103:10",
  "RiptOPL:ript-core-03:core:3078:5",
  "RiptOPL:ript-core-04.b:core:2922:5",
  "Launchstation:launch-core-04.b:core:3198:5",
  "RiptOPL:ript-core-07:core:2471:19",
  "RiptOPL:ript-core-08.b:core:2134:1",
  "RiptOPL:ript-core-09.b:core:2621:1",
  "RiptOPL:ript-core-11:core:3389:8",
  "RiptOPL:ript-core-12:core:2516:9",
  "Launchstation:launch-core-09.b:core:3371:4",
  "Launchstation:launch-core-10:core:3450:6",
  "RiptOPL:ript-core-15.b:core:3068:8",
  "RiptOPL:ript-core-17:core:2128:3",
  "Launchstation:launch-core-12.b:core:4103:1",
  "RiptOPL:ript-core-19:core:3033:5",
  "Launchstation:launch-core-14:core:3291:2",
  "RiptOPL:ript-core-21.b:core:2524:5",
  "RiptOPL:ript-core-22.b:core:2791:4",
  "RiptOPL:ript-core-24:core:3562:5",
  "RiptOPL:ript-core-25:core:2873:3",
  "Launchstation:launch-core-18:core:2259:1",
  "RiptOPL:ript-core-27.b:core:2759:8",
  "RiptOPL:ript-core-28.b:core:3474:2",
  "RiptOPL:ript-core-30:core:3446:13",
  "Launchstation:launch-core-21.b:core:2232:14",
  "Launchstation:launch-core-22:core:3226:2",
  "RiptOPL:ript-core-33.b:core:1646:2",
  "RiptOPL:ript-core-34.b:core:3485:3",
  "RiptOPL:ript-core-36:core:3454:8",
  "Launchstation:launch-core-25.b:core:3242:11",
  "Launchstation:launch-core-26:core:3051:8",
  "Launchstation:launch-core-27:core:3433:10",
  "RiptOPL:ript-core-40.b:core:1737:7",
  "Launchstation:launch-core-28.b:core:3356:13",
  "Launchstation:launch-core-30:core:2127:6",
  "RiptOPL:ript-module-01.b:module:3181:19",
  "Launchstation:launch-module-01.b:module:4524:18",
  "Launchstation:launch-module-02:module:3607:34",
  "RiptOPL:ript-module-06.b:module:3490:19",
  "RiptOPL:ript-module-07.b:module:4610:26",
  "RiptOPL:ript-module-09:module:4690:18",
  "Launchstation:launch-module-05.b:module:4898:19",
  "Launchstation:launch-module-06.b:module:4763:31",
  "RiptOPL:ript-module-13:module:3385:21",
  "RiptOPL:ript-module-14.b:module:4645:31",
  "Launchstation:launch-module-09:module:4025:22",
  "RiptOPL:ript-module-17:module:3928:19",
  "Launchstation:launch-module-10.b:module:3855:5",
  "Launchstation:launch-module-11.b:module:4754:26",
  "Launchstation:launch-module-12:module:2781:15",
  "Launchstation:launch-module-13:module:4398:1",
  "RiptOPL:ript-module-22.b:module:3704:15",
  "Launchstation:launch-module-14.b:module:3714:5",
  "RiptOPL:ript-module-25.b:module:1688:17",
  "RiptOPL:ript-support-01:support:7942:45",
  "RiptOPL:ript-support-04:support:7898:44",
  "RiptOPL:ript-support-06:support:7613:32",
  "Launchstation:launch-support-06:support:5092:43",
  "RiptOPL:ript-support-11:support:6197:43",
  "RiptOPL:ript-docs-02:docs:7635:28",
  "Launchstation:launch-skim-02:skim:2942:2",
  "RiptOPL:ript-skim-04:skim:205284:280"
 ],
 "wave-D": [
  "RiptOPL:ript-core-01.b:core:2266:36",
  "RiptOPL:ript-core-02.b:core:3103:10",
  "Launchstation:launch-core-03:core:3003:3",
  "RiptOPL:ript-core-05.b:core:2636:8",
  "Launchstation:launch-core-05:core:3377:5",
  "RiptOPL:ript-core-07.b:core:2471:19",
  "RiptOPL:ript-core-09:core:2621:1",
  "Launchstation:launch-core-07:core:3455:3",
  "RiptOPL:ript-core-11.b:core:3389:8",
  "RiptOPL:ript-core-12.b:core:2516:9",
  "RiptOPL:ript-core-14:core:2508:11",
  "Launchstation:launch-core-10.b:core:3450:6",
  "RiptOPL:ript-core-16:core:3322:6",
  "RiptOPL:ript-core-17.b:core:2128:3",
  "RiptOPL:ript-core-18.b:core:2726:2",
  "RiptOPL:ript-core-19.b:core:3033:5",
  "RiptOPL:ript-core-21:core:2524:5",
  "Launchstation:launch-core-15.b:core:3414:6",
  "Launchstation:launch-core-16:core:2884:8",
  "Launchstation:launch-core-17:core:2630:2",
  "RiptOPL:ript-core-25.b:core:2873:3",
  "Launchstation:launch-core-18.b:core:2259:1",
  "Launchstation:launch-core-19.b:core:2546:2",
  "RiptOPL:ript-core-29.b:core:3453:6",
  "Launchstation:launch-core-21:core:2232:14",
  "RiptOPL:ript-core-31.b:core:1208:4",
  "Launchstation:launch-core-22.b:core:3226:2",
  "RiptOPL:ript-core-34:core:3485:3",
  "RiptOPL:ript-core-35.b:core:3177:7",
  "RiptOPL:ript-core-36.b:core:3454:8",
  "RiptOPL:ript-core-38:core:1565:11",
  "Launchstation:launch-core-26.b:core:3051:8",
  "Launchstation:launch-core-27.b:core:3433:10",
  "Launchstation:launch-core-28:core:3356:13",
  "Launchstation:launch-core-29.b:core:3098:9",
  "RiptOPL:ript-module-02.b:module:4932:34",
  "RiptOPL:ript-module-04:module:3065:19",
  "RiptOPL:ript-module-05.b:module:4803:21",
  "Launchstation:launch-module-03.b:module:3092:6",
  "RiptOPL:ript-module-08.b:module:4752:19",
  "Launchstation:launch-module-05:module:4898:19",
  "RiptOPL:ript-module-10.b:module:3833:7",
  "RiptOPL:ript-module-12:module:5000:35",
  "Launchstation:launch-module-07.b:module:3199:13",
  "Launchstation:launch-module-08:module:4070:20",
  "RiptOPL:ript-module-15.b:module:4001:17",
  "RiptOPL:ript-module-16.b:module:3732:25",
  "RiptOPL:ript-module-18:module:4932:25",
  "RiptOPL:ript-module-19:module:3128:19",
  "RiptOPL:ript-module-20:module:3550:51",
  "RiptOPL:ript-module-21:module:3785:21",
  "RiptOPL:ript-module-22:module:3704:15",
  "RiptOPL:ript-module-23.b:module:4753:33",
  "Launchstation:launch-module-15:module:1593:5",
  "RiptOPL:ript-module-25:module:1688:17",
  "RiptOPL:ript-buildci-02:buildci:2222:12",
  "RiptOPL:ript-support-02:support:7916:41",
  "RiptOPL:ript-support-05:support:7958:36",
  "Launchstation:launch-support-05:support:7884:59",
  "RiptOPL:ript-support-10:support:7894:82",
  "Launchstation:launch-docs-02:docs:4679:51",
  "Launchstation:launch-skim-01:skim:27149:4",
  "RiptOPL:ript-skim-03:skim:24099:7"
 ]
}

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
