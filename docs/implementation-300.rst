Implementation refinement: 300 passes
=====================================

This campaign uses focused implementation, contract and verification passes,
not 300 new services or 300 independent reviewers. Status is emitted for every
numbered pass. Builds and commits occur at 50-pass checkpoints, followed by
independent kernel, embedded-port and verification/documentation reviews. Review
findings are addressed in following passes and rechecked before the final push.
Existing uncommitted project work is preserved in the first checkpoint.

Passes 001-050: bounded stack observations
--------------------------------------------------------------------------------

001. Preserve the existing requested implementation as the checkpoint baseline.
002. Retain boot-only registration and permanent finished-thread storage.
003. Retain finite IPC deadlines and copied-message lifetime contracts.
004. Review the ARM BASEPRI/SVC atomic enrollment-and-park contract.
005. Choose explicit budgeted watermark scans outside scheduling/snapshots.
006. Paint stacks before the architecture constructs their initial frame.
007. Add allocation-free bounded prefix scanning.
008. Default watermark instrumentation off and validate its compile-time switch.
009. Add a result type without enlarging thread control blocks.
010. Add an explicit diagnostic API with a 256-byte default allowance.
011. Account separately for the optional one-word guard read.
012. Preserve output when the thread identity is unregistered.
013. Expose a metadata-only disabled result.
014. Include initial frame and guard reservation in paint-derived use.
015. Document pattern collisions and unwritten reserved frames.
016. Cover zero-length paint.
017. Cover empty scans without dereferencing a pointer.
018. Cover complete-extent painting.
019. Cover zero-budget read accounting.
020. Cover a single-word allowance.
021. Cover incomplete prefix estimates.
022. Cover the exact full-budget boundary.
023. Cover oversized allowances without expanding the memory extent.
024. Cover an immediate mismatch.
025. Cover a mismatch within a one-word allowance.
026. Cover a mismatch just outside the budget.
027. Cover a mismatch at the budget boundary.
028. Verify the scanner leaves source memory unchanged.
029. Cover a change at the highest stack word.
030. Demonstrate the observable limitation of paint collisions.
031. Cover a guarded subextent.
032. Cover a mismatch immediately above the guard.
033. Cover repeatable painting without hidden state.
034. Exhaust small mismatch positions and read allowances.
035. Cover an empty extent with maximum allowance.
036. Enable observations in the ARM stress preset.
037. Exercise unregistered identity rejection on ARM.
038. Exercise a zero-byte allowance on a real thread stack.
039. Exercise a sub-word allowance on ARM.
040. Observe all real worker and idle stacks.
041. Check reported capacity conservation.
042. Emit stack use/capacity in simulator logs.
043. Add implementation requirement NXR-151.
044. Separate stack guards from watermark documentation.
045. Document scan latency and incomplete observations.
046. Record numbered passes and the checkpoint/review policy here.
047. Regenerate StrictDoc-format and requirements traceability views.
048. Explicitly keep the nano preset's instrumentation disabled.
049. Inspect source diffs and static-allocation boundaries before the checkpoint.
050. Build, validate and commit the first checkpoint, then request panel review.

Passes 051-100: panel repairs and branch coverage
--------------------------------------------------------------------------------

051. Accept public stack-API validation gaps identified by the kernel reviewer.
052. Accept ARM initializer-section rejection gaps identified by the port reviewer.
053. Accept process/drain timeout gaps identified by the tooling reviewer.
054. Accept missing executed-dependency identities in UART manifests.
055. Record all panel decisions and their disposition.
056. Make ARM priority initializer matching explicit and add regression coverage.
057. Reject ARM preinitializer sections.
058. Reject ARM finalizer/destructor sections.
059. Check boot-stack reserves in the ARM linker.
060. Reject finalizers consistently on RV32.
061. Recognize dotted priority-initializer symbols in the audit.
062. Recognize reentrant newlib allocator hooks.
063. Generalize compiler contract checks for RV32 and Cortex-M3.
064. Enable ARM static_contract_check.
065. Add a priority-constructor rejection fixture.
066. Add a priority initializer-subsection fixture.
067. Add an explicit preinitializer fixture.
068. Add a priority preinitializer fixture.
069. Add a finalizer rejection fixture.
070. Extend symbol-audit self-tests.
071. Share a bounded subprocess executor between simulation runners.
072. Extend the deadline across process exit and both pipe drains.
073. Capture output with cancellable fixed-size reads.
074. Bound retained output across both channels.
075. Clean up a still-running owned process after failure.
076. Preserve partial diagnostic output.
077. Use the shared executor in both runners.
078. Include shared source in native-tool fingerprints.
079. Link the XDoc-documented shared source into both projects.
080. Add a normal-exit subprocess fixture.
081. Add nonzero-exit propagation coverage.
082. Add an execution-timeout fixture.
083. Add a descendant-held-pipe fixture.
084. Add an output-flood/cap fixture.
085. Add native process self-tests to CTest with an outer timeout.
086. Isolate managed UART verification output per run.
087. Identify managed dependencies and runtime configuration.
088. Identify cocotb libraries and hostfxr.
089. Identify runtime files, .NET environment, test selector and seed.
090. Reject dependencies changed during a UART run.
091. Read real ARM BASEPRI in the workload.
092. Check API restoration of an existing scheduling mask.
093. Check preservation of a stronger interrupt mask.
094. Check mask restoration across a suspended semaphore wait.
095. Check timer progress while that masked thread is suspended.
096. Observe stack state before scheduler start.
097. Observe the live current thread with a short allowance.
098. Add watermark-off and guard-off ARM presets.
099. Update requirements, documentation and review dispositions.
100. Build, validate and commit the second checkpoint, then request panel review.

Passes 101-150: register, timer and process boundaries
--------------------------------------------------------------------------------

101. Accept the panel's synchronous console forwarding defect.
102. Accept the orphan-process documentation limitation.
103. Close the prior stack API validation requests with retained evidence.
104. Select register and timer boundary coverage.
105. Correct the priority-constructor work record.
106. Remove blocking live-console forwarding from capture.
107. Add an unread outer-pipe regression fixture.
108. Preserve bounded output in logs without live forwarding.
109. Qualify process-tree termination guarantees.
110. Record the independent panel decisions.
111. Separate blocking-call and interrupt-only ABI contracts.
112. Add distinct r4-r11 canaries across SVC and sleep.
113. Inject a callee-saved register mismatch.
114. Add interrupt-only r0-r12 and LR canaries.
115. Snapshot and check NZCV before comparison alters flags.
116. Require timer progress across the positive canary loop.
117. Restore caller ABI state in each harness.
118. Exercise a four-byte-aligned PSP.
119. Check the hardware xPSR padding flag.
120. Explicitly enable exception stack alignment.
121. Configure the initial CMSDK count separately.
122. Preserve the full 32-bit reload modulus.
123. Add a near-rollover preset.
124. Check initial rollover was crossed.
125. Mask SysTick across five periods.
126. Check elapsed-tick catch-up after restoration.
127. Bound catch-up by the independent counter.
128. Configure stress queue capacity.
129. Use the configured stress tick rate.
130. Add faster-tick and three-slot queue variation.
131. Validate new CMake numeric values.
132. Document canary stack layouts.
133. Document register coverage and ABI limits.
134. Distinguish negative probes from positive results.
135. Document padding observation timing.
136. Document timer ownership and limits.
137. Document simulation presets.
138. Add register and timer requirements.
139. Update Sphinx navigation and the work ledger.
140. Inspect firmware storage costs and removal of unused harnesses.
141. Check reservations and restore paths.
142. Check NZCV snapshot instruction ordering.
143. Check initial counter value versus reload modulus.
144. Hash QEMU alongside firmware in ARM manifests.
145. Reject changed inputs before publishing ARM success.
146. Require explicit register and timer coverage records.
147. Update the process fixture result description.
148. Regenerate requirements and StrictDoc-format views.
149. Inspect diffs and no-heap boundaries.
150. Build, validate and commit, then request independent panel review.

Passes 151-200: discriminating witnesses and IPC campaigns
--------------------------------------------------------------------------------

151. Accept catch-up discrimination and masking-baseline findings.
152. Sample the baseline only after masking.
153. Observe the first handler's elapsed count.
154. Require bulk accounting rather than repeated single ticks.
155. Add an optional interrupted-PC witness.
156. Require interruption in the live register window.
157. Inject an independent NZCV mismatch.
158. Observe the initial near-wrap counter word.
159. Check monotonic extended time and an observed epoch.
160. Default witness storage and calls OFF outside stress presets.
161. Add a peer blocked before the masked interval.
162. Check catch-up removes its wait enrollment.
163. Check its resumed result is timeout.
164. Add reproducible fixed-seed IPC stress.
165. Add a shifting-array FIFO oracle.
166. Exercise capacities one, three and eight.
167. Check FIFO order and empty-output preservation.
168. Add token conservation as an independent scalar oracle.
169. Check semaphore maximum overflow.
170. Add sticky-event reference state.
171. Check any/all waits and consumption.
172. Check atomic fetch and exchange prior values.
173. Check successful compare/exchange.
174. Check mismatch updates expected without changing the word.
175. Exercise ISR/thread context boundaries.
176. Report seed, step and action on randomized failures.
177. Add four seeds to CTest.
178. Enroll three waiters in all permutations.
179. Predict priority/FIFO grant order independently.
180. Change a blocked waiter's priority before granting.
181. Cross tick wrap in waiter deadlines.
182. Require payload pointer detachment on timeout.
183. Supply late tokens/messages after expiry.
184. Cover all 27 priority assignments.
185. Cover all six enrollment orders.
186. Combine timeout and priority-change variants.
187. Check critical balance after every campaign.
188. Document host-model verification limits.
189. Document stronger ARM observers and catch-up checks.
190. Record panel repairs in requirements and review notes.
191. Inspect reference-state storage and heap boundaries.
192. Make kernel mask comments portable across both ports.
193. Document atomic load/store serialization.
194. Document exchange/bit-operation return values.
195. Inspect ordinary-build exclusion of witnesses.
196. Inspect timeout and unchanged-output assertions.
197. Inspect finite campaign bounds and repeatability.
198. Regenerate source-traced requirements views.
199. Inspect diffs before building.
200. Build, validate and commit, then request the panel review.
