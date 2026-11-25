package su.xash.engine.provider

import android.database.Cursor
import android.database.MatrixCursor
import android.os.CancellationSignal
import android.os.ParcelFileDescriptor
import android.os.StatFs
import android.provider.DocumentsContract.Document
import android.provider.DocumentsContract.Root
import android.provider.DocumentsProvider
import android.text.format.Formatter
import android.webkit.MimeTypeMap
import su.xash.engine.R
import java.io.File
import java.io.FileNotFoundException
import java.io.IOException

class XashDocumentsProvider : DocumentsProvider() {
	companion object {
		private const val ROOT_ID = "root"
		private val ROOT_COLUMNS = arrayOf(
			Root.COLUMN_ROOT_ID,
			Root.COLUMN_DOCUMENT_ID,
			Root.COLUMN_FLAGS,
			Root.COLUMN_ICON,
			Root.COLUMN_TITLE,
			Root.COLUMN_SUMMARY
		)
		private val DOCUMENT_COLUMNS = arrayOf(
			Document.COLUMN_DOCUMENT_ID,
			Document.COLUMN_DISPLAY_NAME,
			Document.COLUMN_MIME_TYPE,
			Document.COLUMN_FLAGS,
			Document.COLUMN_SIZE,
			Document.COLUMN_LAST_MODIFIED
		)
	}

	private val rootDir: File
		get() = (context!!.getExternalFilesDir(null) ?: context!!.filesDir).also {
			if (!it.exists()) it.mkdirs()
		}

	private fun resolve(documentId: String): File {
		val root = rootDir.canonicalPath
		val file = if (documentId == ROOT_ID) File(root) else File(root, documentId)
		val canonical = file.canonicalPath

		if (canonical != root && !canonical.startsWith(root + File.separator))
			throw FileNotFoundException(documentId)

		return File(canonical)
	}

	private fun documentId(file: File): String {
		val root = rootDir.canonicalPath
		val canonical = file.canonicalPath

		if (canonical == root) return ROOT_ID

		return canonical.removePrefix(root + File.separator)
	}

	private fun addRow(cursor: MatrixCursor, projection: Array<String>?, file: File, isRoot: Boolean) {
		val columns = projection ?: DOCUMENT_COLUMNS
		val row = arrayOfNulls<Any>(columns.size)

		fun put(column: String, value: Any?) {
			val index = columns.indexOf(column)
			if (index >= 0) row[index] = value
		}

		put(Document.COLUMN_DOCUMENT_ID, documentId(file))
		put(Document.COLUMN_DISPLAY_NAME, if (isRoot) context!!.getString(R.string.app_name) else file.name)
		put(Document.COLUMN_LAST_MODIFIED, file.lastModified())

		if (isRoot || file.isDirectory) {
			put(Document.COLUMN_MIME_TYPE, Document.MIME_TYPE_DIR)
			put(Document.COLUMN_FLAGS, if (isRoot) {
				Document.FLAG_DIR_SUPPORTS_CREATE
			} else {
				Document.FLAG_DIR_SUPPORTS_CREATE or Document.FLAG_SUPPORTS_DELETE or
					Document.FLAG_SUPPORTS_RENAME or Document.FLAG_SUPPORTS_MOVE
			})
			put(Document.COLUMN_SIZE, 0)
		} else {
			put(Document.COLUMN_MIME_TYPE, documentType(file))
			put(Document.COLUMN_FLAGS, Document.FLAG_SUPPORTS_WRITE or Document.FLAG_SUPPORTS_DELETE or
				Document.FLAG_SUPPORTS_RENAME or Document.FLAG_SUPPORTS_MOVE or Document.FLAG_SUPPORTS_COPY)
			put(Document.COLUMN_SIZE, file.length())
		}

		cursor.addRow(row)
	}

	private fun documentType(file: File): String {
		if (file.isDirectory) return Document.MIME_TYPE_DIR

		val extension = file.name.substringAfterLast('.', "")
		return MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension.lowercase()) ?: "*/*"
	}

	private fun childName(name: String): String {
		val trimmed = name.trim()

		if (trimmed.isEmpty() || trimmed.contains('/') || trimmed.contains('\u0000') ||
			trimmed == "." || trimmed == "..") {
			throw IOException("Invalid document name")
		}

		return trimmed
	}

	override fun onCreate(): Boolean = true

	override fun queryRoots(projection: Array<String>?): Cursor {
		val columns = projection ?: ROOT_COLUMNS
		val cursor = MatrixCursor(columns)
		val row = arrayOfNulls<Any>(columns.size)

		fun put(column: String, value: Any?) {
			val index = columns.indexOf(column)
			if (index >= 0) row[index] = value
		}

		val free = Formatter.formatShortFileSize(context, StatFs(rootDir.path).availableBytes)

		put(Root.COLUMN_ROOT_ID, ROOT_ID)
		put(Root.COLUMN_DOCUMENT_ID, ROOT_ID)
		put(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_CREATE or Root.FLAG_SUPPORTS_IS_CHILD)
		put(Root.COLUMN_ICON, R.mipmap.ic_launcher)
		put(Root.COLUMN_TITLE, context!!.getString(R.string.app_name))
		put(Root.COLUMN_SUMMARY, "$free free")

		cursor.addRow(row)
		return cursor
	}

	override fun queryDocument(documentId: String, projection: Array<String>?): Cursor {
		val cursor = MatrixCursor(projection ?: DOCUMENT_COLUMNS)
		addRow(cursor, projection, resolve(documentId), documentId == ROOT_ID)
		return cursor
	}

	override fun queryChildDocuments(parentDocumentId: String, projection: Array<String>?, sortOrder: String?): Cursor {
		val parent = resolve(parentDocumentId)

		if (!parent.isDirectory) throw FileNotFoundException(parentDocumentId)

		val children = parent.listFiles()?.toList() ?: emptyList()
		val cursor = MatrixCursor(projection ?: DOCUMENT_COLUMNS, children.size)
		val sorted = when (sortOrder) {
			Document.COLUMN_LAST_MODIFIED + " DESC" -> children.sortedByDescending { it.lastModified() }
			Document.COLUMN_LAST_MODIFIED -> children.sortedBy { it.lastModified() }
			else -> children.sortedBy { it.name.lowercase() }
		}

		for (child in sorted) addRow(cursor, projection, child, false)

		return cursor
	}

	override fun getDocumentType(documentId: String): String = documentType(resolve(documentId))

	override fun openDocument(documentId: String, mode: String, signal: CancellationSignal?): ParcelFileDescriptor {
		val file = resolve(documentId)

		if (!file.isFile) throw FileNotFoundException(documentId)

		return ParcelFileDescriptor.open(file, ParcelFileDescriptor.parseMode(mode))
	}

	override fun createDocument(parentDocumentId: String, mimeType: String, displayName: String): String {
		val parent = resolve(parentDocumentId)

		if (!parent.isDirectory) throw FileNotFoundException(parentDocumentId)

		val name = childName(displayName)
		val file = File(parent, name)

		if (file.exists()) throw IOException("Document already exists")

		val directory = mimeType == Document.MIME_TYPE_DIR || mimeType == "vnd.android.document/directory"

		if (if (directory) file.mkdir() else file.createNewFile()) return documentId(file)

		throw IOException("Failed to create $name")
	}

	override fun deleteDocument(documentId: String) {
		if (documentId == ROOT_ID) throw IOException("Cannot delete root")

		val file = resolve(documentId)

		if (file.exists() && !file.deleteRecursively()) throw IOException("Failed to delete $documentId")
	}

	override fun renameDocument(documentId: String, displayName: String): String {
		if (documentId == ROOT_ID) throw IOException("Cannot rename root")

		val file = resolve(documentId)
		val target = File(file.parentFile, childName(displayName))

		if (target.exists()) throw IOException("Document already exists")

		if (file.renameTo(target)) return documentId(target)

		throw IOException("Failed to rename $documentId")
	}

	override fun moveDocument(sourceDocumentId: String, sourceParentDocumentId: String, targetParentDocumentId: String): String {
		if (sourceDocumentId == ROOT_ID) throw IOException("Cannot move root")

		val source = resolve(sourceDocumentId)
		val targetDir = resolve(targetParentDocumentId)

		if (!targetDir.isDirectory) throw FileNotFoundException(targetParentDocumentId)

		val target = File(targetDir, source.name)

		if (target.exists()) throw IOException("Document already exists")

		if (source.renameTo(target)) return documentId(target)

		throw IOException("Failed to move $sourceDocumentId")
	}

	override fun isChildDocument(parentDocumentId: String, documentId: String): Boolean {
		val parent = resolve(parentDocumentId).canonicalPath
		return resolve(documentId).canonicalPath.startsWith(parent + File.separator)
	}
}
