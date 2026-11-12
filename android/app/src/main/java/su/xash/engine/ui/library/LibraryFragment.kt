package su.xash.engine.ui.library

import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.view.MenuProvider
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Collections
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference
import java.util.concurrent.LinkedBlockingQueue
import su.xash.engine.R
import su.xash.engine.adapters.GameAdapter
import su.xash.engine.databinding.DialogInstallProgressBinding
import su.xash.engine.databinding.FragmentLibraryBinding
import java.io.File
import java.io.FileOutputStream


class LibraryFragment : Fragment(), MenuProvider {
	private var _binding: FragmentLibraryBinding? = null
	private val binding get() = _binding!!

	private val libraryViewModel: LibraryViewModel by activityViewModels()

	private val pickGameFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
		if (uri != null) {
			installGame(uri)
		}
	}

	override fun onCreateView(
		inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?
	): View {
		_binding = FragmentLibraryBinding.inflate(inflater, container, false)

		val adapter = GameAdapter(libraryViewModel)
		binding.gamesList.adapter = adapter

		requireActivity().addMenuProvider(this, viewLifecycleOwner, Lifecycle.State.RESUMED)

		return binding.root
	}

	override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
		binding.swipeRefresh.setOnRefreshListener { libraryViewModel.reloadGames(requireContext()) }

		libraryViewModel.isReloading.observe(viewLifecycleOwner) {
			binding.swipeRefresh.isRefreshing = it
		}

		libraryViewModel.installedGames.observe(viewLifecycleOwner) { games ->
			(binding.gamesList.adapter as GameAdapter).submitList(games)
			val empty = games.isEmpty()
			binding.gamesList.visibility = if (empty) View.GONE else View.VISIBLE
			binding.emptyView.visibility = if (empty) View.VISIBLE else View.GONE
		}

		libraryViewModel.reloadGames(requireContext())
	}

	override fun onDestroyView() {
		super.onDestroyView()
		_binding = null
	}

	override fun onCreateMenu(menu: Menu, menuInflater: MenuInflater) {
		menuInflater.inflate(R.menu.menu_library, menu)
	}

	override fun onMenuItemSelected(menuItem: MenuItem): Boolean {
		when (menuItem.itemId) {
			R.id.action_install_game -> {
				pickGameFolder.launch(null)
				return true
			}
			R.id.action_settings -> {
				findNavController().navigate(R.id.action_libraryFragment_to_appSettingsFragment)
			}
		}

		return false
	}

	override fun onResume() {
		super.onResume()

		libraryViewModel.reloadGames(requireContext())
	}

	private data class CopyItem(val src: DocumentFile, val dest: File, val bytes: Long)

	private sealed interface InstallPlan {
		data class Ok(val targets: List<Pair<DocumentFile, File>>, val items: List<CopyItem>, val totalBytes: Long) : InstallPlan
		data class Error(val message: String) : InstallPlan
		data class Exists(val name: String) : InstallPlan
	}

	private companion object {
		const val WORKER_THREADS = 8
	}

	// heavy Storage Access Framework walk - must run off the UI thread
	private suspend fun computeInstallPlan(ctx: android.content.Context, treeUri: Uri, scanCount: AtomicLong, cancelled: AtomicBoolean): InstallPlan {
		val root = DocumentFile.fromTreeUri(ctx, treeUri)
			?: return InstallPlan.Error("Could not open the selected folder.")

		if (!root.isDirectory) {
			return InstallPlan.Error("The selected item is not a folder.")
		}

		// figure out what to copy where:
		// - the picked folder is a game folder  -> copy into files/<folder>
		// - the picked folder holds game folders -> copy each game folder into files/
		// - anything else                        -> copy as files/<folder>
		val appFiles = appFilesDir(ctx)
		val targets = mutableListOf<Pair<DocumentFile, File>>()

		if (findGameInfoFile(root) != null) {
			targets.add(root to File(appFiles, sanitize(root.name) ?: "game"))
		} else {
			val gameChildren = (root.listFiles() ?: emptyArray()).filter {
				it.isDirectory && findGameInfoFile(it) != null
			}

			if (gameChildren.isNotEmpty()) {
				for (child in gameChildren) {
					targets.add(child to File(appFiles, sanitize(child.name) ?: "game"))
				}
			} else {
				targets.add(root to File(appFiles, sanitize(root.name) ?: "game"))
			}
		}

		val colliding = targets.map { it.second }.firstOrNull { it.exists() }
		if (colliding != null) {
			return InstallPlan.Exists(colliding.name)
		}

		// parallel breadth-first walk, bounded by WORKER_THREADS workers
		val items = Collections.synchronizedList(mutableListOf<CopyItem>())
		val totalBytes = AtomicLong(0)
		val queue = LinkedBlockingQueue<Pair<DocumentFile, File>>()
		val pending = AtomicInteger(targets.size)

		for ((srcRoot, destRoot) in targets) {
			queue.put(srcRoot to destRoot)
		}

		coroutineScope {
			repeat(WORKER_THREADS) {
				launch(Dispatchers.IO) {
					while (true) {
						if (cancelled.get()) {
							throw CancellationException()
						}

						val item = queue.poll()
						if (item == null) {
							if (pending.get() == 0) {
								return@launch
							}
							delay(20)
							continue
						}

						val (src, dest) = item
						val children = src.listFiles() ?: emptyArray()
						for (child in children) {
							if (cancelled.get()) {
								throw CancellationException()
							}

							val childDest = File(dest, sanitize(child.name) ?: "file")
							if (child.isDirectory) {
								pending.incrementAndGet()
								queue.put(child to childDest)
							} else if (child.isFile) {
								val size = child.length()
								synchronized(items) {
									items.add(CopyItem(child, childDest, size))
								}
								totalBytes.addAndGet(size)
								scanCount.incrementAndGet()
							}
						}
						pending.decrementAndGet()
					}
				}
			}
		}

		return InstallPlan.Ok(targets, synchronized(items) { ArrayList(items) }, totalBytes.get())
	}

	private fun appFilesDir(ctx: android.content.Context): File =
		ctx.getExternalFilesDir(null) ?: ctx.filesDir

	private fun sanitize(name: String?): String? =
		name?.replace('/', '_')?.replace('\\', '_')?.takeIf { it.isNotBlank() }

	private fun findGameInfoFile(dir: DocumentFile?): DocumentFile? {
		if (dir == null || !dir.isDirectory) {
			return null
		}
		return dir.listFiles().firstOrNull {
			it.isFile && (
				it.name.equals("liblist.gam", ignoreCase = true) ||
					it.name.equals("gameinfo.txt", ignoreCase = true)
			)
		}
	}

	private fun installGame(treeUri: Uri) {
		val ctx = requireContext()
		val resolver = ctx.contentResolver

		val cancelled = AtomicBoolean(false)
		val installBinding = DialogInstallProgressBinding.inflate(LayoutInflater.from(ctx))
		installBinding.installProgress.isIndeterminate = true
		installBinding.installStatus.text = getString(R.string.preparing_install)
		val dialog = MaterialAlertDialogBuilder(ctx)
			.setTitle(R.string.installing_game)
			.setView(installBinding.root)
			.setCancelable(false)
			.setNegativeButton(R.string.install_cancel) { _, _ -> cancelled.set(true) }
			.create()
		dialog.show()

		val scanCount = AtomicLong(0)
		val startedAt = System.currentTimeMillis()
		// live scan counter so the dialog never looks frozen
		val ticker = requireActivity().lifecycleScope.launch {
			while (true) {
				delay(500)
				installBinding.installStatus.text = getString(
					R.string.preparing_install_progress,
					scanCount.get(),
					(System.currentTimeMillis() - startedAt) / 1000
				)
			}
		}

		// activity scope: survives fragment view destruction/recreation
		requireActivity().lifecycleScope.launch {
			var failure: Throwable? = null
			var plan: InstallPlan? = null

			withContext(Dispatchers.IO) {
				try {
					plan = computeInstallPlan(ctx, treeUri, scanCount, cancelled)
				} catch (e: CancellationException) {
					// user cancelled during scanning
				} catch (e: Throwable) {
					failure = e
				}
			}
			ticker.cancel()

			if (cancelled.get()) {
				dialog.dismiss()
				Toast.makeText(ctx, R.string.install_cancelled, Toast.LENGTH_SHORT).show()
				return@launch
			}

			if (failure != null) {
				dialog.dismiss()
				MaterialAlertDialogBuilder(ctx)
					.setTitle(R.string.install_failed_title)
					.setMessage(getString(R.string.install_failed_message, failure!!.message))
					.setPositiveButton(android.R.string.ok, null)
					.show()
				return@launch
			}

			when (val p = plan!!) {
				is InstallPlan.Error -> {
					dialog.dismiss()
					MaterialAlertDialogBuilder(ctx)
						.setTitle(R.string.install_failed_title)
						.setMessage(getString(R.string.install_failed_message, p.message))
						.setPositiveButton(android.R.string.ok, null)
						.show()
					return@launch
				}
				is InstallPlan.Exists -> {
					dialog.dismiss()
					MaterialAlertDialogBuilder(ctx)
						.setTitle(R.string.install_exists_title)
						.setMessage(getString(R.string.install_exists_message, p.name))
						.setPositiveButton(android.R.string.ok, null)
						.show()
					return@launch
				}
				is InstallPlan.Ok -> {
					installBinding.installProgress.isIndeterminate = false
					installBinding.installProgress.progress = 0

					val targets = p.targets
					val items = p.items
					val totalBytes = p.totalBytes
					val totalFiles = items.size

					val copiedBytes = AtomicLong(0)
					val copiedFiles = AtomicLong(0)
					val lastUpdate = AtomicLong(0)
					val copyFailure = AtomicReference<Throwable?>(null)

					// parallel copy: split items into WORKER_THREADS interleaved chunks
					val chunks = Array(WORKER_THREADS) { mutableListOf<CopyItem>() }
					for ((i, item) in items.withIndex()) {
						chunks[i % WORKER_THREADS].add(item)
					}

					coroutineScope {
						val workers = chunks.map { chunk ->
							async(Dispatchers.IO) {
								for (item in chunk) {
									if (cancelled.get() || copyFailure.get() != null) {
										return@async
									}

									try {
										item.dest.parentFile?.mkdirs()
										resolver.openInputStream(item.src.uri)?.use { input ->
											FileOutputStream(item.dest).use { output ->
												input.copyTo(output, 1024 * 1024)
											}
										} ?: throw IllegalStateException("Could not read ${item.src.name ?: item.src.uri}")

										val bytes = copiedBytes.addAndGet(item.bytes)
										val files = copiedFiles.incrementAndGet()

										val now = System.currentTimeMillis()
										if (now - lastUpdate.get() > 60L || files == totalFiles.toLong()) {
											lastUpdate.set(now)
											val fileName = item.src.name ?: "file"
											withContext(Dispatchers.Main) {
												installBinding.installStatus.text =
													getString(R.string.installing_status, fileName)
												installBinding.installDetail.text = getString(
													R.string.installing_detail, files.toInt(), totalFiles,
													bytes / (1024 * 1024), totalBytes / (1024 * 1024)
												)
												installBinding.installProgress.progress =
													if (totalBytes > 0) ((bytes * 100) / totalBytes).toInt() else 0
											}
										}
									} catch (e: Throwable) {
										copyFailure.compareAndSet(null, e)
										return@async
									}
								}
							}
						}
						workers.awaitAll()
					}

					failure = copyFailure.get()
					dialog.dismiss()

					when {
						failure != null -> {
							for ((_, dest) in targets) {
								dest.deleteRecursively()
							}
							MaterialAlertDialogBuilder(ctx)
								.setTitle(R.string.install_failed_title)
								.setMessage(getString(R.string.install_failed_message, failure!!.message))
								.setPositiveButton(android.R.string.ok, null)
								.show()
							libraryViewModel.reloadGames(ctx)
						}
						cancelled.get() -> {
							for ((_, dest) in targets) {
								dest.deleteRecursively()
							}
							Toast.makeText(ctx, R.string.install_cancelled, Toast.LENGTH_SHORT).show()
							libraryViewModel.reloadGames(ctx)
						}
						else -> {
							Toast.makeText(ctx, R.string.install_finished, Toast.LENGTH_SHORT).show()
							libraryViewModel.reloadGames(ctx)
						}
					}
				}
			}
		}
	}
}
