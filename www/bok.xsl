<?xml version="1.0"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:template match="/">
    <html>
      <head></head>
      <body>
        <h1>Books</h1>
        <table border="1">
          <tr>
            <th>Title</th> <th>BookID</th> <th>AuthorID</th>
          </tr>
          <xsl:for-each select="books/book">
            <xsl:sort select="title"/>
              <tr>
              <td><xsl:value-of select="title"/></td>
              <td><xsl:value-of select="bookID"/></td>
              <td><xsl:value-of select="authorID"/></td>
            </tr>
          </xsl:for-each>
        </table>
      </body>
    </html>
  </xsl:template>
</xsl:stylesheet>

