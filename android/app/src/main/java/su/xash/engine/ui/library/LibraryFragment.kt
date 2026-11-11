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
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
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

	private fun appFilesDir(): File =
		requireContext().getExternalFilesDir(null) ?: requireContext().filesDir

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

	private fun collectItems(src: DocumentFile, dest: File, sink: MutableList<CopyItem>): Long {
		var totalBytes = 0L
		if (src.isDirectory) {
			for (child in src.listFiles() ?: emptyArray()) {
				totalBytes += collectItems(child, File(dest, sanitize(child.name) ?: "file"), sink)
			}
		} else if (src.isFile) {
			val size = src.length()
			sink.add(CopyItem(src, dest, size))
			totalBytes += size
		}
		return totalBytes
	}

	private fun installGame(treeUri: Uri) {
		val ctx = requireContext()
		val resolver = ctx.contentResolver

		val root = DocumentFile.fromTreeUri(ctx, treeUri)
		if (root == null || !root.isDirectory) {
			MaterialAlertDialogBuilder(ctx)
				.setTitle(R.string.install_failed_title)
				.setMessage(getString(R.string.install_failed_message, "Could not open the selected folder."))
				.setPositiveButton(android.R.string.ok, null)
				.show()
			return
		}

		// figure out what to copy where:
		// - the picked folder is a game folder  -> copy into files/<folder>
		// - the picked folder holds game folders -> copy each game folder into files/
		// - anything else                        -> copy as files/<folder>
		val appFiles = appFilesDir()
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
			MaterialAlertDialogBuilder(ctx)
				.setTitle(R.string.install_exists_title)
				.setMessage(getString(R.string.install_exists_message, colliding.name))
				.setPositiveButton(android.R.string.ok, null)
				.show()
			return
		}

		val items = mutableListOf<CopyItem>()
		var totalBytes = 0L
		for ((srcRoot, dest) in targets) {
			totalBytes += collectItems(srcRoot, dest, items)
		}

		var cancelled = false
		val installBinding = DialogInstallProgressBinding.inflate(LayoutInflater.from(ctx))
		val dialog = MaterialAlertDialogBuilder(ctx)
			.setTitle(R.string.installing_game)
			.setView(installBinding.root)
			.setCancelable(false)
			.setNegativeButton(R.string.install_cancel) { _, _ -> cancelled = true }
			.create()
		dialog.show()

		viewLifecycleOwner.lifecycleScope.launch {
			var copiedBytes = 0L
			var copiedFiles = 0
			var lastUpdate = 0L
			var failure: Throwable? = null

			withContext(Dispatchers.IO) {
				try {
					for (item in items) {
						if (cancelled) {
							break
						}

						item.dest.parentFile?.mkdirs()
						resolver.openInputStream(item.src.uri)?.use { input ->
							FileOutputStream(item.dest).use { output ->
								input.copyTo(output, 128 * 1024)
							}
						} ?: throw IllegalStateException("Could not read ${item.src.name ?: item.src.uri}")

						copiedBytes += item.bytes
						copiedFiles++

						val now = System.currentTimeMillis()
						if (now - lastUpdate > 60L || copiedFiles == items.size) {
							lastUpdate = now
							withContext(Dispatchers.Main) {
								installBinding.installStatus.text =
									getString(R.string.installing_status, item.src.name ?: "file")
								installBinding.installDetail.text = getString(
									R.string.installing_detail, copiedFiles, items.size,
									copiedBytes / (1024 * 1024), totalBytes / (1024 * 1024)
								)
								installBinding.installProgress.progress =
									if (totalBytes > 0) ((copiedBytes * 100) / totalBytes).toInt() else 0
							}
						}
					}
				} catch (e: Throwable) {
					failure = e
				}
			}

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
				cancelled -> {
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
